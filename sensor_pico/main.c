/*
 * main.c - Sensor Pico prototype (Pico W, bare metal, Pico SDK 2.x)
 *
 *   GP20 button  take one reading and encode it into the RAM page image
 *   GP21 button  flush: append a CHECK and program the RAM image to flash
 *   GP22 button  print the raw flash log to serial (USB), as hex
 *   GP0/GP1      I2C0 to the AHT10
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

#include "pico/stdlib.h"
#include "hardware/i2c.h"

#include "aht10.h"
#include "encoder.h"
#include "flash_log.h"

#define I2C_PORT        i2c0
#define I2C_SDA         0
#define I2C_SCL         1

#define PIN_SAMPLE      20
#define PIN_FLUSH       21
#define PIN_DUMP        22

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
    enc_result_t r = enc_put_start(&enc, t, FAKE_UNIX_TIME, FAKE_INTERVAL, true);
    if (r == ENC_NO_ROOM && close_page())
        r = enc_put_start(&enc, t, FAKE_UNIX_TIME, FAKE_INTERVAL, true);
    if (r == ENC_OK) need_start = false;
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

/* ------------------------------------------------------------------ buttons */

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

/* ------------------------------------------------------------------ main */

int main(void) {
    stdio_init_all();
    sleep_ms(2000);                             /* give the USB serial monitor time to connect */

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

    button_t b_sample, b_flush, b_dump;
    button_init(&b_sample, PIN_SAMPLE);
    button_init(&b_flush,  PIN_FLUSH);
    button_init(&b_dump,   PIN_DUMP);
    sleep_ms(10);                               /* let the pull-ups settle */
    button_sync(&b_sample);
    button_sync(&b_flush);
    button_sync(&b_dump);

    printf("GP20 = sample, GP21 = flush to flash, GP22 = dump flash\n");

    while (true) {
        if (button_pressed(&b_sample)) do_sample();
        if (button_pressed(&b_flush))  do_flush();
        if (button_pressed(&b_dump))   do_dump();
        sleep_ms(POLL_MS);
    }
}
