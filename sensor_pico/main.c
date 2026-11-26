/*
 * main.c - Sensor Pico prototype (Pico W, bare metal, Pico SDK 2.x)
 *
 *   GP20 button  take one reading and encode it into the RAM page image
 *   GP21 button  flush: append a CHECK and program the RAM image to flash
 *   GP22 button  print the raw flash log to serial (USB), as hex
 *   GP0/GP1      I2C0 to the AHT10
 *   GP4/GP5      UART1 TX/RX to the Wi-Fi Pico's GP1/GP0
 *
 * UART CONFIG also starts periodic sampling. Retrieval flushes the current RAM
 * page, then sends saved flash bytes through the unchanged shared protocol.
 * Local buttons retain their original sample/flush/dump actions.
 *
 * Buttons go between the pin and GND (internal pull-ups, active low).
 * Serial output is over USB: the default UART stdio would collide with the I2C pins.
 *
 * The first successful reading after power-up writes BOOT + TIMEBASE + anchor, using the
 * placeholder time below, since there is no real time source yet.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/flash.h"
#include "pico/stdio_usb.h"

#include "aht10.h"
#include "encoder.h"
#include "flash_log.h"
#include "sensor_uart.h"
#include "protocol/crc16.h"
#include "protocol/storage_profile.h"
#ifdef SENSOR_LIGHT_SLEEP
#include "low_power.h"
#endif
#ifdef SENSOR_BATCHED
#include "protocol/log_digest.h"
static uint8_t frozen_digest[32];
static uint32_t frozen_length;
#endif

#define I2C_PORT        i2c0
#define I2C_SDA         0
#define I2C_SCL         1

#define PIN_SAMPLE      20
#define PIN_FLUSH       21
#define PIN_DUMP        22

#define AUTO_FLUSH_MS   3600000u                /* flush at a sample wake after each hour */
#define POLL_MS         5
#define DEBOUNCE_TICKS  6                       /* 6 x 5 ms = 30 ms of a stable level */

#define FAKE_UNIX_TIME  1792208220u             /* placeholder UTC seconds */
#define FAKE_INTERVAL   60u                     /* placeholder interval, minutes */

#ifndef ERASE_LOG_ON_BOOT
#define ERASE_LOG_ON_BOOT 0                     /* set to 1 ONCE on a fresh board, then back to 0 */
#endif

static enc_t enc;
static bool  need_start = true;                 /* next good reading writes BOOT + TIMEBASE + anchor */
static bool  log_stopped = false;               /* region full or a flash write failed */
static bool storage_ready, storage_failed, sampling, configured, boot_written;
static bool snapshot_held, clear_active, dumping;
static uint8_t snapshot_bytes[STORAGE_TRANSFER_CAPACITY];
static size_t snapshot_length;
static uint32_t samples_saved;
static uint32_t config_timestamp = FAKE_UNIX_TIME;
static uint32_t config_interval_seconds = FAKE_INTERVAL * 60u;
static uint64_t config_started_ms, next_sample_ms, next_auto_flush_ms;
#ifdef SENSOR_LIGHT_SLEEP
static uint64_t armed_sample_ms;
static uint32_t sleep_entries, sample_alarms;
#endif

static uint64_t now_ms(void) { return time_us_64() / 1000u; }

/* PC time anchors the log; elapsed Pico time supplies later TIMEBASE records.
 * There is still no external RTC, and configuration is not persisted yet. */
static uint32_t log_timestamp(void) {
    if (!configured) return FAKE_UNIX_TIME;
    uint64_t seconds = config_timestamp + (now_ms() - config_started_ms) / 1000u;
    return seconds > UINT32_MAX ? UINT32_MAX : (uint32_t)seconds;
}

/* ------------------------------------------------------------------ helpers */

static void print_temp(int16_t t) {
    int a = t < 0 ? -t : t;
    printf("%s%d.%02d C", t < 0 ? "-" : "", a / 100, a % 100);
}

static const char *flog_str(flog_status_t s) {
    switch (s) {
        case FLOG_OK:          return "programmed";
        case FLOG_UNCHANGED:   return "unchanged (already in flash)";
        case FLOG_ERR_FULL:    return "log region full";
        case FLOG_ERR_ILLEGAL: return "refused: would need a 0->1 change";
        case FLOG_ERR_PROGRAM: return "program failed";
        case FLOG_ERR_VERIFY:  return "verify failed";
        case FLOG_ERR_OVERLAP: return "program image overlaps the log region";
    }
    return "?";
}

/* ------------------------------------------------------------------ logging */

/* Seal and program the current page, then move on to a fresh one. */
static bool close_page(void) {
    enc_seal(&enc);
    flog_status_t s = flog_program_page(enc.page);
    if (s != FLOG_OK && s != FLOG_UNCHANGED) {
        printf("close page: %s - logging stopped\n", flog_str(s));
        log_stopped = true;
        storage_failed = true;
        return false;
    }
    uint32_t done = flog_current_page();
    if (!flog_advance()) {
        printf("Log region full after page %u - logging stopped\n", (unsigned)done);
        log_stopped = true;
        return false;
    }
    enc_reset_page(&enc);                       /* the next sample becomes an anchor */
    printf("page %u closed, now on page %u\n", (unsigned)done, (unsigned)flog_current_page());
    return true;
}

static void store_start(int16_t t) {
    uint32_t timestamp = log_timestamp();
    uint16_t minutes = (uint16_t)(config_interval_seconds / 60u);
    enc_result_t r = enc_put_start(&enc, t, timestamp, minutes, !boot_written);
    if (r == ENC_NO_ROOM && close_page())
        r = enc_put_start(&enc, t, timestamp, minutes, !boot_written);
    if (r == ENC_OK) {
        need_start = false;
        boot_written = true;
        ++samples_saved;
    }
}

static void store_sample(int16_t t) {
    enc_result_t r = enc_put_sample(&enc, t);
    if (r == ENC_NO_ROOM && close_page())
        r = enc_put_sample(&enc, t);
    if (r != ENC_OK) printf("sample not stored\n");
    else ++samples_saved;
}

static void store_error(void) {
    enc_result_t r = enc_put_error(&enc);
    if (r == ENC_NO_ROOM && close_page())
        r = enc_put_error(&enc);
    if (r != ENC_OK) printf("error record not stored\n");
}

/* ------------------------------------------------------------------ button actions */

static void do_sample(void) {
    if (clear_active) { printf("[sample] flash CLEAR busy\n"); return; }
    int16_t t = 0;
    aht10_status_t st = aht10_read_temp(I2C_PORT, &t);

    if (log_stopped) {
        printf("[sample] log stopped, reading dropped (%s)\n", aht10_status_str(st));
        return;
    }
    if (st != AHT10_OK) {
        printf("[sample] read failed: %s\n", aht10_status_str(st));
        if (need_start) printf("         no TIMEBASE yet, nothing logged\n");
        else            store_error();
    } else {
        printf("[sample] ");
        print_temp(t);
        printf("  q=%d\n", enc_temp_to_q(t));
        if (need_start) store_start(t);
        else            store_sample(t);
    }
    printf("         page %u, %u of %u nibbles, %s\n",
           (unsigned)flog_current_page(), (unsigned)enc.nib, (unsigned)ENC_PAGE_NIBBLES,
           enc_has_unsealed(&enc) ? "unsealed data" : "sealed");
}

static void do_flush(void) {
    if (clear_active) { printf("[flush] flash CLEAR busy\n"); return; }
    if (log_stopped) { printf("[flush] log stopped\n"); return; }
    bool sealed = enc_seal(&enc);
    flog_status_t s = flog_program_page(enc.page);
    printf("[flush] page %u: %s%s\n", (unsigned)flog_current_page(), flog_str(s),
           sealed ? ", CHECK appended" : "");
    if (s != FLOG_OK && s != FLOG_UNCHANGED) {
        log_stopped = true;
        storage_failed = true;
    }
}

/* Output format, one line per 32 bytes:  @<page>:<offset> <hex bytes>
 * Save the serial output to a file and decode it with tools/decode.py. */
static void do_dump(void) {
    if (clear_active) { printf("[dump] flash CLEAR busy\n"); return; }
    uint8_t buf[FLOG_PAGE_BYTES];
    uint32_t pages = 0;
    dumping = true;

    printf("FLASHDUMP BEGIN region=%u bytes, %u pages\n",
           (unsigned)FLOG_REGION_BYTES, (unsigned)FLOG_PAGE_COUNT);
    for (uint32_t p = 0; p < FLOG_PAGE_COUNT; p++) {
        if (flog_page_is_empty(p)) break;       /* pages are used in order: stop at the first empty one */
        flog_read_page(p, buf);
        for (uint32_t off = 0; off < FLOG_PAGE_BYTES; off += 32) {
            /* Same USB dump format, one bounded write per line. Keep servicing
             * UART retries while printing a large flash log. */
            char line[120];
            int used = snprintf(line, sizeof(line), "@%04u:%03u", (unsigned)p, (unsigned)off);
            for (uint32_t i = 0; i < 32; i++)
                used += snprintf(line + used, sizeof(line) - (size_t)used, " %02X", buf[off + i]);
            printf("%s\n", line);
            sensor_uart_poll();
        }
        pages++;
    }
    printf("FLASHDUMP END pages=%u\n", (unsigned)pages);
    if (enc_has_unsealed(&enc))
        printf("(RAM holds data that is not in flash yet: press the flush button)\n");
    dumping = false;
}

/* ------------------------------------------------------------------ UART storage connection */

static bool uart_configure(void *context, uint32_t timestamp, uint32_t seconds) {
    (void)context;
    if (!storage_ready || storage_failed || log_stopped || clear_active || dumping ||
        snapshot_held || !seconds || seconds % 60u ||
        seconds / 60u > ENC_MAX_INTERVAL_MIN) return false;
    config_timestamp = timestamp;
    config_interval_seconds = seconds;
    config_started_ms = now_ms();
    next_sample_ms = config_started_ms;
    next_auto_flush_ms = config_started_ms + AUTO_FLUSH_MS;
    configured = sampling = true;
    need_start = true; /* Keep old readings; record the new time/interval. */
    printf("[CONFIG] interval=%" PRIu32 " seconds; timestamp=%" PRIu32 " (PC time, no RTC)\n",
           seconds, timestamp);
    return true;
}

static bool uart_snapshot(void *context, const uint8_t **bytes, size_t *length) {
    (void)context;
    if (!storage_ready || storage_failed || clear_active || dumping || snapshot_held) return false;
    /* Preserve GP21's manual flush action, but do not require a second person
     * to press it before a remote retrieval. The transmitted source is flash. */
    if (enc.nib && !log_stopped) do_flush();
    if (storage_failed) return false;
    const uint8_t *flash;
    size_t total;
    if (!flog_data_view(&flash, &total)) return false;
    if (total > sizeof(snapshot_bytes)) {
        printf("[LOG] Retrieval refused: flash has %u bytes, transfer capacity=%u; batching required\n",
               (unsigned)total, (unsigned)sizeof(snapshot_bytes));
        return false; /* Never silently truncate the flash log. */
    }
    memcpy(snapshot_bytes, flash, total);
    snapshot_length = total;
    snapshot_held = true;
    *bytes = snapshot_bytes;
    *length = total;
    printf("[LOG] Flash snapshot: bytes=%u pages=%u crc16_x25=%04x\n",
           (unsigned)total, (unsigned)(total / FLOG_PAGE_BYTES), crc16_x25(snapshot_bytes, total));
    return true;
}

static void uart_release(void *context, bool completed) {
    (void)context;
    snapshot_held = false;
    printf("[LOG] Flash snapshot released: %s; flash retained\n",
           completed ? "final ACK received" : "transfer aborted");
}

#ifdef SENSOR_BATCHED
static bool sensor_open_batch(void *context, const batch_info_t *request,
                              batch_info_t *result, uint8_t *buffer) {
    (void)context;
    if (!storage_ready || storage_failed || clear_active || dumping) return false;
    bool fresh = batch_digest_zero(request->sha256);
    uint32_t total;
    uint8_t digest[32];
    if (snapshot_held) {
        total = frozen_length;
        memcpy(digest, frozen_digest, sizeof(digest));
        if (!fresh && (request->total != total || memcmp(request->sha256, digest, 32))) return false;
    } else {
        if (fresh && enc.nib && !log_stopped) {
            /* Close/advance the page: future sampling cannot change this prefix. */
            (void)close_page();
            need_start = true;
        }
        if (storage_failed) return false;
        size_t available;
        const uint8_t *flash;
        if (!flog_data_view(&flash, &available)) return false;
        total = fresh ? available : request->total;
        if (total > available || total > UART_BATCH_MAX_LOG || total % FLOG_PAGE_BYTES) return false;
        log_digest(flash, total, digest);
        if (!fresh && memcmp(request->sha256, digest, sizeof(digest))) return false;
    }
    if (request->offset > total || request->offset % UART_BATCH_BYTES ||
        (total && request->offset == total)) return false;
    *result = *request;
    result->total = total;
    memcpy(result->sha256, digest, sizeof(digest));
    uint32_t left = total - request->offset;
    result->length = (uint16_t)(left < UART_BATCH_BYTES ? left : UART_BATCH_BYTES);
    if (result->length) memcpy(buffer, flog_data() + request->offset, result->length);
    result->crc = crc16_x25(buffer, result->length);
    frozen_length = total;
    memcpy(frozen_digest, digest, sizeof(digest));
    snapshot_held = true;
    printf("[BATCH] Frozen flash total=%" PRIu32 " offset=%" PRIu32 " bytes=%u; source retained\n",
           total, request->offset, result->length);
    return true;
}
static bool sensor_finish_batch(void *context, const uint8_t digest[32], uint32_t total) {
    (void)context;
    if (!storage_ready || storage_failed || clear_active || dumping) return false;
    const uint8_t *flash;
    size_t available;
    if (!flog_data_view(&flash, &available) || total > available) return false;
    uint8_t actual[32];
    if (snapshot_held) {
        if (total != frozen_length || memcmp(digest, frozen_digest, 32)) return false;
    } else {
        log_digest(flash, total, actual);
        if (memcmp(actual, digest, 32)) return false;
    }
    snapshot_held = false;
    printf("[BATCH] Complete log delivery confirmed; Sensor flash retained\n");
    return true;
}
#endif

static pico1_storage_result_t uart_clear_begin(void *context) {
    (void)context;
    if (!storage_ready || snapshot_held || clear_active || dumping ||
        !flog_erase_begin()) return P1_STORAGE_ERROR;
    sampling = false;
    clear_active = true;
    printf("[CLEAR] Erasing and verifying flash; Sensor GP22 remains USB dump\n");
    return P1_STORAGE_PENDING;
}

static pico1_storage_result_t uart_clear_poll(void *context) {
    (void)context;
    if (!clear_active) return P1_STORAGE_ERROR;
    /* One sector per loop, highest first as in the original logger. Interrupts
     * may still pause during each SDK flash operation; lost frames use retries.
     * Erasure is not power-fail atomic. Never ACK a partial or failed erase. */
    bool complete;
    if (!flog_erase_step(&complete)) {
        clear_active = false;
        log_stopped = storage_failed = true;
        printf("[CLEAR] Failed erase/read-back; no success ACK; retry CLEAR to recover\n");
        return P1_STORAGE_ERROR;
    }
    if (!complete) return P1_STORAGE_PENDING;
    if (flog_init() != FLOG_OK || flog_current_page() != 0) {
        clear_active = false;
        log_stopped = storage_failed = true;
        return P1_STORAGE_ERROR;
    }
    enc_reset_page(&enc);
    need_start = true;
    log_stopped = storage_failed = false;
    snapshot_length = 0;
    clear_active = false;
    printf("[CLEAR] Flash erase verified; RAM cleared; periodic sampling paused\n");
    return P1_STORAGE_READY;
}

static void sampling_poll(void) {
    if (!sampling || clear_active || log_stopped || now_ms() < next_sample_ms) return;
    /* Manual GP20 samples remain additional debug readings. */
    uint64_t started = now_ms();
    do_sample();
    next_sample_ms = started + (uint64_t)config_interval_seconds * 1000u;
    if (started >= next_auto_flush_ms) {
        if (enc_has_unsealed(&enc)) do_flush();
        next_auto_flush_ms = started + AUTO_FLUSH_MS;
    }
}

#ifdef SENSOR_LIGHT_SLEEP
static bool work_pending(void) {
    return snapshot_held || clear_active || dumping || !sensor_uart_can_sleep() ||
           (sampling && !log_stopped && now_ms() >= next_sample_ms);
}

static void idle_with_light_sleep(void) {
    uint64_t deadline = sampling && !log_stopped ? next_sample_ms : 0;
    if (deadline != armed_sample_ms) {
        if (deadline) lp_arm_timer(from_us_since_boot(deadline * 1000u));
        else lp_cancel_timer();
        armed_sample_ms = deadline;
    }
    if (work_pending()) {
        sleep_ms(POLL_MS); /* Keep polling UART retries and verified erase while busy. */
        return;
    }
    /* Recheck under interrupt masking after GP5 wake has been armed. */
    bool slept = lp_sleep_until_event(work_pending);
    if (slept) ++sleep_entries;
    if (lp_take_uart_wake() && slept) sensor_uart_woke();
}
#endif

static void print_status(void) {
#ifdef SENSOR_BATCHED
    printf("[START] Sensor Pico flash logger + batched UART; bare-metal\n");
#else
    printf("[START] Sensor Pico flash logger + shared UART; bare-metal\n");
#endif
    printf("GP20 = sample, GP21 = flush to flash, GP22 = dump flash\n");
#ifdef SENSOR_BATCHED
    printf("[LOG] flash=ON RTC=OFF format=interval12; batch=4096 bytes maximum_log=1048576 bytes\n");
#else
    printf("[LOG] flash=ON RTC=OFF format=interval12; transfer capacity=%u bytes\n",
           (unsigned)sizeof(snapshot_bytes));
#endif
    printf("[LOG] page=%u samples_this_boot=%" PRIu32 " sampling=%u stopped=%u clear=%u\n",
           (unsigned)flog_current_page(), samples_saved, sampling ? 1u : 0u,
           log_stopped ? 1u : 0u, clear_active ? 1u : 0u);
#ifdef SENSOR_BATCHED
    printf("[MODE] BATCHED: 4096 bytes/range, maximum 1 MiB, prefix SHA256 resume; flash retained\n");
#endif
#ifdef SENSOR_LIGHT_SLEEP
    printf("[SLEEP] Light sleep enabled; entries=%" PRIu32 " sample alarms=%" PRIu32 "\n",
           sleep_entries, sample_alarms);
#endif
    sensor_uart_report();
    printf("[USB] s: status; d: flash dump. Wi-Fi CONFIG starts periodic readings\n");
}

static void console_poll(void) {
    static bool connected_before;
    bool connected = stdio_usb_connected();
    if (connected && !connected_before) print_status();
    connected_before = connected;
    if (!connected) return;
    for (unsigned i = 0; i < 16; ++i) {
        int c = getchar_timeout_us(0);
        if (c == PICO_ERROR_TIMEOUT) break;
        if (c == 's' || c == 'S') print_status();
        if (c == 'd' || c == 'D') do_dump();
    }
}

/* ------------------------------------------------------------------ buttons */

#ifndef SENSOR_LIGHT_SLEEP
typedef struct {
    uint    pin;
    bool    stable;                             /* debounced level: true = released */
    uint8_t count;
} button_t;

static void button_init(button_t *b, uint pin) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_pull_up(pin);
    b->pin = pin;
    b->count = 0;
    b->stable = true;
}

static void button_sync(button_t *b) { b->stable = gpio_get(b->pin); }

/* Call every POLL_MS. Returns true once per press (a stable high -> low change). */
static bool button_pressed(button_t *b) {
    bool raw = gpio_get(b->pin);
    if (raw == b->stable) { b->count = 0; return false; }
    if (++b->count < DEBOUNCE_TICKS) return false;
    b->stable = raw;
    b->count = 0;
    return !raw;
}
#endif

/* ------------------------------------------------------------------ main */

int main(void) {
    stdio_init_all();
    static const pico1_backend_t backend = {
        .configure = uart_configure, .snapshot = uart_snapshot,
        .clear_begin = uart_clear_begin, .clear_poll = uart_clear_poll,
        .release = uart_release
    };
    enc_reset_page(&enc);
#ifdef SENSOR_BATCHED
    if (!sensor_uart_init_batched(&backend, (batch_source_t){.open = sensor_open_batch,
                                                   .finish = sensor_finish_batch})) return 1;
#else
    if (!sensor_uart_init(&backend)) return 1;
#endif
    sleep_ms(250); /* USB may settle, but it is never required for UART operation. */

    i2c_init(I2C_PORT, 100 * 1000);             /* 100 kHz */
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    aht10_status_t as = aht10_init(I2C_PORT);
    printf("\nSensor Pico prototype\nAHT10 init: %s\n", aht10_status_str(as));

#if ERASE_LOG_ON_BOOT
    printf("Erasing the log region...\n");
    if (!flog_erase_all()) {
        printf("ERROR: boot erase/verification failed\n");
        return 1;
    }
#endif

    flog_status_t fs = flog_init();
    if (fs == FLOG_ERR_OVERLAP) {
        printf("ERROR: %s\n", flog_str(fs));
        while (true) tight_loop_contents();
    }
    if (fs == FLOG_ERR_FULL) {
        printf("Log region is full\n");
        log_stopped = true;
    }
    storage_ready = true;
    printf("Log region: %u KiB, writing at page %u of %u\n",
           (unsigned)(FLOG_REGION_BYTES / 1024u),
           (unsigned)flog_current_page(), (unsigned)FLOG_PAGE_COUNT);

    enc_reset_page(&enc);

#ifdef SENSOR_LIGHT_SLEEP
    lp_init();
    if (!lp_add_button(PIN_SAMPLE) || !lp_add_button(PIN_FLUSH) || !lp_add_button(PIN_DUMP))
        return 1;
    sensor_uart_enable_wake();
    printf("[SLEEP] Light sleep enabled; wake sources: timer, buttons, UART RX GP5\n");
#else
    button_t b_sample, b_flush, b_dump;
    button_init(&b_sample, PIN_SAMPLE);
    button_init(&b_flush,  PIN_FLUSH);
    button_init(&b_dump,   PIN_DUMP);
    sleep_ms(10);                               /* let the pull-ups settle */
    button_sync(&b_sample);
    button_sync(&b_flush);
    button_sync(&b_dump);
#endif

    printf("GP20 = sample, GP21 = flush to flash, GP22 = dump flash\n");

    while (true) {
        sensor_uart_poll();
#ifdef SENSOR_LIGHT_SLEEP
        if (lp_take_timer()) ++sample_alarms;
        if (lp_take_button(PIN_SAMPLE)) do_sample();
        if (lp_take_button(PIN_FLUSH))  do_flush();
        if (lp_take_button(PIN_DUMP))   do_dump();
#else
        if (button_pressed(&b_sample)) do_sample();
        if (button_pressed(&b_flush))  do_flush();
        if (button_pressed(&b_dump))   do_dump();
#endif
        sampling_poll();
        console_poll();
#ifdef SENSOR_LIGHT_SLEEP
        idle_with_light_sleep();
#else
        sleep_ms(POLL_MS);
#endif
    }
}
