/*
 * main.c - Sensor Pico prototype (Pico W, bare metal, Pico SDK 2.x)
 *
 *   GP20 button  take one reading and encode it into the RAM page image (also wakes the chip)
 *   GP21 button  flush: append a CHECK and program the RAM image to flash (also wakes the chip)
 *   GP22 button  print the raw flash log to serial (USB), as hex (also wakes the chip)
 *   GP0/GP1      I2C0 to the AHT10
 *   GP4/GP5      UART1 protocol link to the Wi-Fi Pico (TX/RX)
 *
 * Between events the chip sleeps (low_power.c). It wakes on: the sample timer, any of the three buttons,
 * or a falling edge on GP5 (RX). The UART is not clocked while asleep, so the Wi-Fi Pico sends a wake
 * preamble first: WAKE_PREAMBLE_BYTES bytes of WAKE_PREAMBLE_VALUE (32 x 0x55, about 2.8 ms at 115200),
 * immediately followed by the frame. The first byte is lost or garbled; the rest arrive as ordinary
 * bytes and the receiver is back in step well before the frame starts. The stream parser must ignore
 * 0x55 outside a frame.
 *
 * Buttons go between the pin and GND (internal pull-ups, active low).
 * Serial output is over USB: the default UART stdio would collide with the I2C pins.
 *
 * The first successful reading after power-up writes BOOT + TIMEBASE + anchor, using the
 * placeholder time below, since there is no real time source yet.
 * A UART CONFIG supplies the time and interval. It is written as a new TIMEBASE (+ anchor) with the next
 * good reading, also when a TIMEBASE already exists in the log. Timer sampling uses the configured
 * interval (or the placeholder interval until a CONFIG arrives); the sample button remains available.
 */

#include <stdio.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdbool.h>

#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/uart.h"

#include "aht10.h"
#include "encoder.h"
#include "flash_log.h"
#include "pico1/pico1_protocol.h"
#include "protocol/stream_parser.h"
#include "low_power.h"


#include "pico-extras/src/rp2_common/pico_sleep/include/pico/sleep.h"

#define I2C_PORT        i2c0
#define I2C_SDA         0
#define I2C_SCL         1

#define SENSOR_UART     uart1
#define SENSOR_TX       4u
#define SENSOR_RX       5u
#define SESSION_ID      0x11223344u
#define RX_CAPACITY     512u
#define RX_POLL_BUDGET  128u

#define PIN_SAMPLE      20
#define PIN_FLUSH       21
#define PIN_DUMP        22
#define BUTTONS_ENABLED 1

#define POLL_MS          5                      /* loop period while awake */
#define UART_LINGER_MS   500u                   /* stay awake this long after the last UART activity */
#define AUTO_FLUSH_PERIOD_MS 3600000u           /* flush at the next sample wake after each hour */

/* The Wi-Fi Pico side must send this before every message (documented here so both ends agree). */
#define WAKE_PREAMBLE_BYTES 32u                 /* sent by the Wi-Fi Pico, no gap before the frame */
#define WAKE_PREAMBLE_VALUE 0x55u               /* alternating bits: a falling edge every bit pair */

#define FAKE_UNIX_TIME  1792208220u             /* placeholder UTC seconds */
#define FAKE_INTERVAL   60u                     /* placeholder interval, minutes */

#ifndef ERASE_LOG_ON_BOOT
#define ERASE_LOG_ON_BOOT 0                     /* set to 1 ONCE on a fresh board, then back to 0 */
#endif

static enc_t enc;
static bool  boot_pending = true;               /* next good reading writes BOOT first (once per log start) */
static bool  tb_pending = true;                 /* next good reading writes TIMEBASE + anchor */
static bool  config_received;
static bool  log_stopped = false;               /* region full or a flash write failed */
static uint32_t log_timestamp = FAKE_UNIX_TIME;
static uint16_t log_interval_minutes = FAKE_INTERVAL;
static absolute_time_t next_sample;
static absolute_time_t next_auto_flush;
static bool snapshot_held;      // prevent buttons and timer from touching flash while wifi pico is doing operations
static bool clear_in_progress;
static pico1_ctx_t endpoint;
static stream_parser_t parser;
static protocol_frame_t frame;
static uint8_t rx[RX_CAPACITY];
static volatile uint32_t rx_head, rx_tail, rx_errors, rx_dropped;

/* ------------------------------------------------------------------ helpers */

static uint32_t sample_period_ms(void) {
    return (uint32_t)log_interval_minutes * 60u * 1000u;
}

static void reschedule_sample_timer(void) {
    next_sample = delayed_by_ms(get_absolute_time(), sample_period_ms());
    lp_arm_timer(next_sample);
}

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

/* The protocol library calls this adapter instead of knowing about Pico UART hardware. */
static void sensor_uart_write(void *context, const uint8_t *bytes, size_t length) {
    (void)context;
    uart_write_blocking(SENSOR_UART, bytes, length);
}

static uint64_t sensor_clock_ms(void *context) {
    (void)context;
    return time_us_64() / 1000u;
}

static void sensor_usb_log(const char *text) {
    printf("%s", text);
}

/* Buffer bytes in the interrupt; parsing and protocol callbacks run in the main loop. */
static void uart_rx_irq(void) {
    while (uart_is_readable(SENSOR_UART)) {
        uint32_t value = uart_get_hw(SENSOR_UART)->dr;
        if (value & 0xf00u) {
            ++rx_errors;
            uart_get_hw(SENSOR_UART)->rsr = 0;
        } else if (rx_head - rx_tail == RX_CAPACITY) {
            ++rx_dropped;
        } else {
            rx[rx_head % RX_CAPACITY] = (uint8_t)value;
            ++rx_head;
        }
    }
}

/* Briefly mask interrupts while advancing the consumer side of the RX ring. */
static bool pop_uart_byte(uint8_t *byte) {
    uint32_t state = save_and_disable_interrupts();
    bool available = rx_tail != rx_head;
    if (available) *byte = rx[rx_tail++ % RX_CAPACITY];
    restore_interrupts(state);
    return available;
}

/* CONFIG intervals arrive in seconds; this encoder stores whole minutes in 1..4095. */
static bool configure_log(void *context, uint32_t timestamp, uint32_t interval_seconds) {
    (void)context;
    if (interval_seconds < 60u || interval_seconds % 60u != 0 ||
        interval_seconds / 60u > ENC_MAX_INTERVAL_MIN) {
        return false;
    }
    log_timestamp = timestamp;
    log_interval_minutes = (uint16_t)(interval_seconds / 60u);
    config_received = true;
    reschedule_sample_timer();
    next_auto_flush = delayed_by_ms(get_absolute_time(), AUTO_FLUSH_PERIOD_MS);
    tb_pending = true;                          /* a newer CONFIG before the next sample replaces this one */
    printf("[UART] %s: timestamp=%" PRIu32 " interval=%u minutes; "
           "written as a TIMEBASE with the next good reading\n",
           boot_pending ? "configured first TIMEBASE" : "new TIMEBASE queued",
           log_timestamp, (unsigned)log_interval_minutes);
    return true;
}

/* ------------------------------------------------------------------ logging */

/* Seal and program the current page, then move on to a fresh one. */
static bool close_page(void) {
    enc_seal(&enc);
    flog_status_t s = flog_program_page(enc.page);
    if (s != FLOG_OK && s != FLOG_UNCHANGED) {
        printf("close page: %s - logging stopped\n", flog_str(s));
        log_stopped = true;
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

/* [BOOT] + TIMEBASE + anchor. BOOT is included only for the first one after power-up or a log clear. */
static void store_start(int16_t t) {
    enc_result_t r = enc_put_start(&enc, t, log_timestamp, log_interval_minutes, boot_pending);
    if (r == ENC_NO_ROOM && close_page())
        r = enc_put_start(&enc, t, log_timestamp, log_interval_minutes, boot_pending);
    if (r == ENC_OK) { boot_pending = false; tb_pending = false; }
    else printf("TIMEBASE not stored (%s)\n", r == ENC_BAD_ARG ? "bad interval" : "no room");
}

static void store_sample(int16_t t) {
    enc_result_t r = enc_put_sample(&enc, t);
    if (r == ENC_NO_ROOM && close_page())
        r = enc_put_sample(&enc, t);
    if (r != ENC_OK) printf("sample not stored\n");
}

static void store_error(void) {
    enc_result_t r = enc_put_error(&enc);
    if (r == ENC_NO_ROOM && close_page())
        r = enc_put_error(&enc);
    if (r != ENC_OK) printf("error record not stored\n");
}

/* ------------------------------------------------------------------ button actions */

static void do_sample(void) {
    if (snapshot_held || clear_in_progress) return;
    int16_t t = 0;
    aht10_status_t st = aht10_read_temp(I2C_PORT, &t);

    if (log_stopped) {
        printf("[sample] log stopped, reading dropped (%s)\n", aht10_status_str(st));
        return;
    }
    if (st != AHT10_OK) {
        printf("[sample] read failed: %s\n", aht10_status_str(st));
        if (boot_pending) printf("         no TIMEBASE yet, nothing logged\n");
        else              store_error();                 /* a pending TIMEBASE waits for a good reading */
    } else {
        printf("[sample] ");
        print_temp(t);
        printf("  q=%d\n", enc_temp_to_q(t));
        if (tb_pending) store_start(t);                  /* BOOT (first time only) + TIMEBASE + anchor */
        else            store_sample(t);
    }
    printf("         page %u, %u of %u nibbles, %s\n",
           (unsigned)flog_current_page(), (unsigned)enc.nib, (unsigned)ENC_PAGE_NIBBLES,
           enc_has_unsealed(&enc) ? "unsealed data" : "sealed");
}

static void do_flush(void) {
    if (snapshot_held || clear_in_progress) return;
    if (log_stopped) { printf("[flush] log stopped\n"); return; }
    bool sealed = enc_seal(&enc);
    flog_status_t s = flog_program_page(enc.page);
    printf("[flush] page %u: %s%s\n", (unsigned)flog_current_page(), flog_str(s),
           sealed ? ", CHECK appended" : "");
    if (s != FLOG_OK && s != FLOG_UNCHANGED) log_stopped = true;
}

/* Output format, one line per 32 bytes:  @<page>:<offset> <hex bytes>
 * Save the serial output to a file and decode it with tools/decode.py. */
static void do_dump(void) {
    uint8_t buf[FLOG_PAGE_BYTES];
    uint32_t pages = 0;

    printf("FLASHDUMP BEGIN region=%u bytes, %u pages\n",
           (unsigned)FLOG_REGION_BYTES, (unsigned)FLOG_PAGE_COUNT);
    for (uint32_t p = 0; p < FLOG_PAGE_COUNT; p++) {
        if (flog_page_is_empty(p)) break;       /* pages are used in order: stop at the first empty one */
        flog_read_page(p, buf);
        for (uint32_t off = 0; off < FLOG_PAGE_BYTES; off += 32) {
            printf("@%04u:%03u", (unsigned)p, (unsigned)off);
            for (uint32_t i = 0; i < 32; i++) printf(" %02X", buf[off + i]);
            printf("\n");
        }
        pages++;
    }
    printf("FLASHDUMP END pages=%u\n", (unsigned)pages);
    if (enc_has_unsealed(&enc))
        printf("(RAM holds data that is not in flash yet: press the flush button)\n");
}

/* Flush a stable page image, then lease its XIP flash bytes until transfer release. */
static bool protocol_snapshot(void *context, const uint8_t **bytes, size_t *length) {
    (void)context;
    if (snapshot_held || clear_in_progress) return false;
    snapshot_held = true;
    if (!flog_is_full()) {
        if (enc_has_unsealed(&enc)) enc_seal(&enc);
        flog_status_t status = flog_program_page(enc.page);
        if (status != FLOG_OK && status != FLOG_UNCHANGED) {
            snapshot_held = false;
            printf("[UART] snapshot flush failed: %s\n", flog_str(status));
            return false;
        }
    }
    if (!flog_data_view(bytes, length)) {
        snapshot_held = false;
        return false;
    }
    if (*length > (size_t)UINT16_MAX * DATA_CHUNK_SIZE) {
        snapshot_held = false;
        printf("[UART] snapshot exceeds protocol limit (%u bytes); retrieval rejected\n",
               (unsigned)((size_t)UINT16_MAX * DATA_CHUNK_SIZE));
        return false;
    }
    printf("[UART] flash snapshot: %u bytes; writes paused until transfer ends\n",
           (unsigned)*length);
    return true;
}

static void protocol_release(void *context, bool completed) {
    (void)context;
    snapshot_held = false;
    printf("[UART] snapshot %s; flash writes resumed\n",
           completed ? "transfer complete" : "transfer released");
}

static pico1_storage_result_t protocol_clear_begin(void *context) {
    (void)context;
    if (snapshot_held || clear_in_progress || !flog_erase_begin())
        return P1_STORAGE_ERROR;
    clear_in_progress = true;
    /* The gateway must extend clear_ack_timeout_ms beyond the full-region erase time. */
    printf("[UART] clearing flash log one sector at a time\n");
    return P1_STORAGE_PENDING;
}

static pico1_storage_result_t protocol_clear_poll(void *context) {
    (void)context;
    bool complete = false;
    if (!flog_erase_step(&complete)) {
        clear_in_progress = false;
        log_stopped = true; /* A partial erase is not a safe place to resume logging. */
        return P1_STORAGE_ERROR;
    }
    if (!complete) return P1_STORAGE_PENDING;

    enc_reset_page(&enc);
    boot_pending = true;
    tb_pending = true;
    log_stopped = false;
    clear_in_progress = false;
    printf("[UART] flash log cleared; next sample starts a new log\n");
    return P1_STORAGE_READY;
}

/* ------------------------------------------------------------------ events */

/* Handle the timer and the three buttons. While the protocol is busy the flash must not be touched,
 * so presses are consumed rather than replayed afterwards. Returns true if anything fired. */
static bool service_events(bool protocol_busy) {
    bool did = false;
    if (lp_take_timer()) { // if timer flag was set in an interrupt
        did = true;
        if (config_received) {
            next_sample = delayed_by_ms(next_sample, sample_period_ms()); // 'time when recieve config' + 'configured sample period'
            lp_arm_timer(next_sample); // sets alarm for when time to do the next sample
            if (!protocol_busy) {
                do_sample();
                if (time_reached(next_auto_flush)) {
                    do_flush();
                    next_auto_flush = delayed_by_ms(get_absolute_time(), AUTO_FLUSH_PERIOD_MS);
                }
            }
        }
    }
    // if gpio flags were set in an interrupt
    if (lp_take_button(PIN_SAMPLE)) {
        if (config_received && !protocol_busy) do_sample();
        did = true;
    }
    if (lp_take_button(PIN_FLUSH))  { if (!protocol_busy) do_flush();  did = true; }
    if (lp_take_button(PIN_DUMP))   { if (!protocol_busy) do_dump();   did = true; }
    return did;
}

/* ------------------------------------------------------------------ main */

int main(void) {
    stdio_init_all();
    gpio_init(23);
    gpio_set_dir(23, GPIO_OUT);
    gpio_put(23, false);

    sleep_ms(10000);                             /* give the USB serial monitor time to connect */
    printf("Initializing sensor pico...\n");
    
    uart_init(SENSOR_UART, 115200);
    gpio_set_function(SENSOR_TX, GPIO_FUNC_UART);
    gpio_set_function(SENSOR_RX, GPIO_FUNC_UART);
    gpio_pull_up(SENSOR_RX);
    uart_set_hw_flow(SENSOR_UART, false, false);
    uart_set_format(SENSOR_UART, 8, 1, UART_PARITY_NONE);
    irq_set_exclusive_handler(UART1_IRQ, uart_rx_irq);
    irq_set_enabled(UART1_IRQ, true);
    uart_set_irq_enables(SENSOR_UART, true, false);
    stream_parser_init(&parser);

    static const pico1_backend_t backend = {
        .context = NULL,
        .configure = configure_log,
        .snapshot = protocol_snapshot,
        .clear_begin = protocol_clear_begin,
        .clear_poll = protocol_clear_poll,
        .release = protocol_release
    };
    protocol_link_t link = {
        .context = NULL,
        .session_id = SESSION_ID,
        .log = sensor_usb_log,
        .write = sensor_uart_write,
        .now_ms = sensor_clock_ms
    };
    if (!pico1_init(&endpoint, link, NULL, 0, (pico1_hooks_t){.backend = &backend})) {
        printf("ERROR: could not initialize UART protocol endpoint\n");
        return 1;
    }

    i2c_init(I2C_PORT, 100 * 1000);             /* 100 kHz */
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    aht10_status_t as = aht10_init(I2C_PORT);
    printf("\nSensor Pico prototype\nAHT10 init: %s\n", aht10_status_str(as));

#if ERASE_LOG_ON_BOOT
    printf("Erasing the log region...\n");
    flog_erase_all();
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
    printf("Log region: %u KiB, writing at page %u of %u\n",
           (unsigned)(FLOG_REGION_BYTES / 1024u),
           (unsigned)flog_current_page(), (unsigned)FLOG_PAGE_COUNT);

    enc_reset_page(&enc);

    lp_init();
#if BUTTONS_ENABLED
    lp_add_button(PIN_SAMPLE);
    lp_add_button(PIN_FLUSH);
    lp_add_button(PIN_DUMP);
#endif
    lp_add_uart_wake(SENSOR_UART, SENSOR_RX);   /* after uart_init and the pin setup above */

    printf("GP20 = sample, GP21 = flush to flash, GP22 = dump flash (all wake the chip)\n");
    printf("Waiting for UART CONFIG before sampling\n");
    printf("[UART] UART1 GP4 TX / GP5 RX; 115200 8N1; session=%08x\n", (unsigned)SESSION_ID);

    uint32_t observed_drops = 0;
    uint32_t seen_rx = rx_head, seen_errors = rx_errors;
    absolute_time_t awake_until = make_timeout_time_ms(UART_LINGER_MS);
    while (true) {
        if (observed_drops != rx_dropped) {
            observed_drops = rx_dropped;
            stream_parser_init(&parser);
            pico1_abort_transfer(&endpoint);
            printf("[UART] RX overflow: dropped=%" PRIu32 "; transfer aborted\n", observed_drops);
        }

        // pulls bytes out of the receive ring, feeds them to the stream parser, and handles each complete frame. 
        // It stops after 128 bytes(defined as RX_POLL_BUDGET) even if more are waiting, 
        // and carries on with the rest of the main loop. 
        // The remaining bytes are picked up on the next pass
        // Bound work per pass so button handling and ACK/retry processing stay responsive.
        for (unsigned n = 0; n < RX_POLL_BUDGET; ++n) {
            uint8_t byte;
            if (!pop_uart_byte(&byte)) break;
            if (!stream_parser_feed(&parser, byte)) continue;
            if (!frame_decode(parser.buffer, parser.length, &frame)) {
                ++rx_errors;
                continue;
            }
            pico1_handle_frame(&endpoint, &frame);
        }
        pico1_tick(&endpoint);

        /* A snapshot points directly into flash, so no event may program or erase it. */
        bool protocol_busy = snapshot_held || clear_in_progress ||
                             endpoint.transfer_state != P1_TRANSFER_IDLE ||
                             endpoint.clear_pending;
        service_events(protocol_busy);

        /* Any UART traffic (or the wake edge itself) keeps the chip awake for a while. */
        if (lp_take_uart_wake() || rx_head != seen_rx || rx_errors != seen_errors) {
            seen_rx = rx_head;
            seen_errors = rx_errors;
            awake_until = make_timeout_time_ms(UART_LINGER_MS);
        }

        if (!protocol_busy && time_reached(awake_until)) {
            lp_sleep_until_event();             /* returns on the timer, a button, UART RX, or any interrupt */
        } else {
            sleep_ms(POLL_MS);
        }
    }
}