/* Generates a long, varied log with the real encoder and prints it as a FLASHDUMP (the GP22 format),
 * preceded by EXPECT lines: "EXPECT <index> <q or -1 for an error> <unix time>".
 * roundtrip.py decodes the dump with tools/decode.py and compares. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "encoder.h"

#define MAXPAGES 64
#define MAXSAMPLES 4000
static uint8_t pages[MAXPAGES][ENC_PAGE_BYTES];
static int npages;
static enc_t e;
static int exp_q[MAXSAMPLES]; static uint32_t exp_t[MAXSAMPLES]; static int nexp;

static void close_page(void) {
    enc_seal(&e);
    memcpy(pages[npages++], e.page, ENC_PAGE_BYTES);
    enc_reset_page(&e);                              /* the next sample becomes an anchor */
}

static unsigned state = 777;
static int rnd(void) { state = state * 1103515245u + 12345u; return (int)((state >> 16) & 0x7FFF); }

int main(void) {
    static const uint16_t intervals[] = { 60, 5, 240, 1, 4095 };
    enc_reset_page(&e);
    uint32_t t0 = 0, interval = 0; int slot = 0;
    int t = 2100;
    bool first = true;

    for (int i = 0; i < 1500; i++) {
        t += (rnd() % 100 < 4) ? (int)(rnd() % 1600) - 800 : (int)(rnd() % 41) - 20;
        if (t < -4500) t = -4500;
        if (t > 9500)  t = 9500;

        if (i % 200 == 0) {                          /* a (new) TIMEBASE, also in the middle of a log */
            t0 = 1792208220u + (uint32_t)i * 977u;
            interval = intervals[(i / 200) % 5];
            slot = 0;
            enc_result_t r = enc_put_start(&e, (int16_t)t, t0, (uint16_t)interval, first);
            if (r == ENC_NO_ROOM) { close_page(); r = enc_put_start(&e, (int16_t)t, t0, (uint16_t)interval, first); }
            if (r != ENC_OK) return 2;
            first = false;
            exp_q[nexp] = enc_temp_to_q((int16_t)t); exp_t[nexp++] = t0 + (uint32_t)slot++ * interval * 60u;
            continue;
        }
        if (i % 97 == 13) {                          /* a failed sensor read */
            enc_result_t r = enc_put_error(&e);
            if (r == ENC_NO_ROOM) { close_page(); r = enc_put_error(&e); }
            if (r != ENC_OK) return 3;
            exp_q[nexp] = -1; exp_t[nexp++] = t0 + (uint32_t)slot++ * interval * 60u;
            continue;
        }
        if (i % 41 == 0) enc_seal(&e);               /* a flush in the middle of a page */
        enc_result_t r = enc_put_sample(&e, (int16_t)t);
        if (r == ENC_NO_ROOM) { close_page(); r = enc_put_sample(&e, (int16_t)t); }
        if (r != ENC_OK) return 4;
        exp_q[nexp] = enc_temp_to_q((int16_t)t); exp_t[nexp++] = t0 + (uint32_t)slot++ * interval * 60u;
    }
    close_page();

    for (int i = 0; i < nexp; i++) printf("EXPECT %d %d %u\n", i, exp_q[i], (unsigned)exp_t[i]);
    printf("FLASHDUMP BEGIN region=%u bytes, %d pages\n", (unsigned)(npages * ENC_PAGE_BYTES), npages);
    for (int p = 0; p < npages; p++)
        for (int off = 0; off < (int)ENC_PAGE_BYTES; off += 32) {
            printf("@%04d:%03d", p, off);
            for (int k = 0; k < 32; k++) printf(" %02X", pages[p][off + k]);
            printf("\n");
        }
    printf("FLASHDUMP END pages=%d\n", npages);
    return 0;
}
