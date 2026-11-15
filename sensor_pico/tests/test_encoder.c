/* Unit tests for encoder.c (pure logic, no SDK). */
#include <stdlib.h>
#include "test.h"
#include "encoder.h"

/* ---- helpers: an independent reference for the format, written from the spec, not from encoder.c ---- */

static unsigned nib(const enc_t *e, unsigned i) {
    return (i & 1) ? (e->page[i >> 1] & 0x0Fu) : (unsigned)(e->page[i >> 1] >> 4);
}

/* CRC-16 CCITT-FALSE fed one nibble at a time, MSB first. */
static uint16_t crc_ref(const enc_t *e, unsigned from, unsigned to) {
    uint16_t crc = 0xFFFF;
    for (unsigned i = from; i < to; i++) {
        crc ^= (uint16_t)(nib(e, i) << 12);
        for (int b = 0; b < 4; b++) crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

/* Decode the samples in a page. Anchors and deltas give q, a SENSOR_ERR gives -1. Returns the count. */
static int decode_q(const enc_t *e, int *out, int max) {
    int n = 0, v = -1;
    unsigned i = 0;
    while (i < e->nib && n < max) {
        unsigned c = nib(e, i++);
        if (c <= 6)       { v += (int)c; out[n++] = v; }
        else if (c >= 9)  { v -= (int)(c - 8); out[n++] = v; }
        else if (c == 8)  { v = (int)((nib(e, i) << 8) | (nib(e, i + 1) << 4) | nib(e, i + 2)); i += 3; out[n++] = v; }
        else {                                          /* 7: metadata */
            unsigned t = nib(e, i++);
            if (t == 1) i += 8 + 3;                     /* TIMEBASE: 8 time + 3 interval nibbles */
            else if (t == 2) out[n++] = -1;             /* SENSOR_ERR */
            else if (t == 4) i += 4;                    /* CHECK */
        }
    }
    return n;
}

static unsigned lcg_state = 12345;
static unsigned rnd(void) { lcg_state = lcg_state * 1103515245u + 12345u; return (lcg_state >> 16) & 0x7FFF; }

/* ---- tests ---- */

static void test_crc_reference_check_value(void) {
    /* Validates the reference itself: "123456789" must give the published check value 0x29B1. */
    const char *s = "123456789";
    uint16_t crc = 0xFFFF;
    for (; *s; s++) {
        crc ^= (uint16_t)((unsigned char)*s << 8);
        for (int b = 0; b < 8; b++) crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    CHECK_EQ(crc, 0x29B1);
}

static void test_temp_to_q(void) {
    CHECK_EQ(enc_temp_to_q(-4000), 0);            /* -40.00 C is the bottom of the range */
    CHECK_EQ(enc_temp_to_q(-5000), 0);            /* below it: clamped */
    CHECK_EQ(enc_temp_to_q(0), 160);              /* 0 C */
    CHECK_EQ(enc_temp_to_q(2100), 244);           /* 21.00 C */
    CHECK_EQ(enc_temp_to_q(3100), 284);           /* 31.00 C */
    CHECK_EQ(enc_temp_to_q(8500), 500);           /* 85.00 C is the top: q = 500 */
    CHECK_EQ(enc_temp_to_q(12000), 500);          /* above it: clamped */
    CHECK_EQ(enc_temp_to_q(2112), 244);           /* rounds to the nearest 0.25 C: 21.12 -> 21.00 */
    CHECK_EQ(enc_temp_to_q(2113), 245);           /*                               21.13 -> 21.25 */
}

static void test_reset_page(void) {
    enc_t e; memset(&e, 0x5A, sizeof e);
    enc_reset_page(&e);
    for (unsigned i = 0; i < ENC_PAGE_BYTES; i++) CHECK_EQ(e.page[i], 0xFF);
    CHECK_EQ(e.nib, 0);
    CHECK(!e.has_anchor);
    CHECK(!enc_has_unsealed(&e));
}

static void test_start_record_bytes(void) {
    enc_t e; enc_reset_page(&e);
    CHECK_EQ(enc_put_start(&e, 2100, 0x6AF292D6u, 1, true), ENC_OK);
    /* 7 3 | 7 1  6AF292D6  001 | 8 0F4  (21.00 C: q = 244 = 0x0F4) */
    const uint8_t want[] = { 0x73, 0x71, 0x6A, 0xF2, 0x92, 0xD6, 0x00, 0x18, 0x0F, 0x4F };
    CHECK_BYTES(e.page, want, sizeof want);
    CHECK_EQ(e.nib, 19);                           /* BOOT 2 + TIMEBASE 13 + anchor 4 */
    CHECK(e.has_anchor);
}

static void test_start_without_boot(void) {
    enc_t e; enc_reset_page(&e);
    CHECK_EQ(enc_put_start(&e, 2100, 0x6AF292D6u, 60, false), ENC_OK);
    CHECK_EQ(e.nib, 17);                           /* no BOOT: TIMEBASE 13 + anchor 4 */
    CHECK_EQ(nib(&e, 0), 7);
    CHECK_EQ(nib(&e, 1), 1);
    CHECK_EQ(nib(&e, 10), 0); CHECK_EQ(nib(&e, 11), 3); CHECK_EQ(nib(&e, 12), 0xC);   /* 060 -> 0x03C */
}

static void test_interval_limits(void) {
    enc_t e; enc_reset_page(&e);
    CHECK_EQ(enc_put_start(&e, 2100, 1000, 0, true), ENC_BAD_ARG);
    CHECK_EQ(enc_put_start(&e, 2100, 1000, 4096, true), ENC_BAD_ARG);    /* does not fit in 3 nibbles */
    CHECK_EQ(e.nib, 0);                                                  /* a refused call writes nothing */
    CHECK_EQ(e.page[0], 0xFF);
    CHECK_EQ(enc_put_start(&e, 2100, 1000, 1, true), ENC_OK);
    enc_reset_page(&e);
    CHECK_EQ(enc_put_start(&e, 2100, 1000, 4095, true), ENC_OK);
    CHECK_EQ(nib(&e, 12), 0xF); CHECK_EQ(nib(&e, 13), 0xF); CHECK_EQ(nib(&e, 14), 0xF);   /* after BOOT + 7 1 + 8 time nibbles */
}

static void test_delta_boundaries(void) {
    enc_t e; enc_reset_page(&e);
    enc_put_start(&e, 2000, 1000, 60, false);                 /* q = 240 */
    unsigned n = e.nib;
    enc_put_sample(&e, 2000);                                 /*  0 steps */
    CHECK_EQ(e.nib, n + 1); CHECK_EQ(nib(&e, n), 0x0);
    enc_put_sample(&e, 2000 + 6 * 25);                        /* +6 steps */
    CHECK_EQ(e.nib, n + 2); CHECK_EQ(nib(&e, n + 1), 0x6);
    enc_put_sample(&e, 2000);                                 /* -6 steps */
    CHECK_EQ(e.nib, n + 3); CHECK_EQ(nib(&e, n + 2), 0xE);
    enc_put_sample(&e, 2000 - 25);                            /* -1 step */
    CHECK_EQ(e.nib, n + 4); CHECK_EQ(nib(&e, n + 3), 0x9);
    enc_put_sample(&e, 2000 - 25 + 7 * 25);                   /* +7 steps: too big, becomes an anchor */
    CHECK_EQ(e.nib, n + 8); CHECK_EQ(nib(&e, n + 4), 0x8);
    enc_put_sample(&e, 2000 - 25 + 7 * 25 - 7 * 25);          /* -7 steps: anchor again */
    CHECK_EQ(e.nib, n + 12); CHECK_EQ(nib(&e, n + 8), 0x8);
}

static void test_sample_before_anchor_is_anchor(void) {
    enc_t e; enc_reset_page(&e);                              /* a fresh page, e.g. after a page change */
    CHECK_EQ(enc_put_sample(&e, 2000), ENC_OK);
    CHECK_EQ(e.nib, 4);
    CHECK_EQ(nib(&e, 0), 0x8);
    CHECK(e.has_anchor);
}

static void test_error_record_keeps_value(void) {
    enc_t e; enc_reset_page(&e);
    enc_put_start(&e, 2000, 1000, 60, false);
    enc_put_error(&e);
    unsigned n = e.nib;
    CHECK_EQ(nib(&e, n - 2), 0x7); CHECK_EQ(nib(&e, n - 1), 0x2);
    enc_put_sample(&e, 2025);                                 /* +1 step relative to the value BEFORE the error */
    CHECK_EQ(e.nib, n + 1); CHECK_EQ(nib(&e, n), 0x1);
}

static void test_round_trip_no_drift(void) {
    enc_t e; enc_reset_page(&e);
    int expect[600]; int ne = 0;
    int t = 2100;
    enc_put_start(&e, (int16_t)t, 1000, 60, true);
    expect[ne++] = enc_temp_to_q((int16_t)t);
    for (int i = 1; i < 600; i++) {
        int r = (int)(rnd() % 100);
        if (r < 3)       t += (int)(rnd() % 2000) - 1000;     /* occasional big jump */
        else             t += (int)(rnd() % 61) - 30;         /* small wander in 0.01 C: rounding accumulates */
        if (t < -4500) t = -4500;
        if (t > 9500)  t = 9500;
        if (i % 17 == 0) { if (enc_put_error(&e) != ENC_OK) break; expect[ne++] = -1; continue; }
        if (i % 41 == 0) enc_seal(&e);
        if (enc_put_sample(&e, (int16_t)t) != ENC_OK) break;
        expect[ne++] = enc_temp_to_q((int16_t)t);
    }
    int got[600];
    int ng = decode_q(&e, got, 600);
    CHECK_EQ(ng, ne);
    CHECK(ne > 200);                                          /* the page really was exercised */
    for (int i = 0; i < ne && i < ng; i++) if (got[i] != expect[i]) { CHECK_EQ(got[i], expect[i]); break; }
}

static void test_page_full_keeps_reserve(void) {
    enc_t e; enc_reset_page(&e);
    enc_put_start(&e, 2000, 1000, 60, true);
    int t = 2000, guard = 0;
    enc_result_t r;
    do {
        t += (guard % 2) ? 700 : -700;                        /* forces anchors: 4 nibbles per sample */
        enc_t before = e;
        r = enc_put_sample(&e, (int16_t)t);
        if (r == ENC_NO_ROOM) CHECK_BYTES(&e, &before, sizeof e);   /* a refused call changes nothing */
    } while (r == ENC_OK && ++guard < 1000);
    CHECK_EQ(r, ENC_NO_ROOM);
    CHECK(e.nib + ENC_CHECK_NIBBLES <= ENC_PAGE_NIBBLES);     /* the CHECK always still fits */
    CHECK(enc_seal(&e));
    CHECK(e.nib <= ENC_PAGE_NIBBLES);
    CHECK_EQ(nib(&e, e.nib - 6), 0x7);
    CHECK_EQ(nib(&e, e.nib - 5), 0x4);
}

static void test_seal_crc_covers_only_new_data(void) {
    enc_t e; enc_reset_page(&e);
    CHECK(!enc_has_unsealed(&e));
    enc_put_start(&e, 2100, 0x6AF292D6u, 60, true);
    enc_put_sample(&e, 2125); enc_put_sample(&e, 2150);
    CHECK(enc_has_unsealed(&e));
    unsigned end1 = e.nib;
    uint16_t want1 = crc_ref(&e, 0, end1);
    CHECK(enc_seal(&e));
    CHECK(!enc_has_unsealed(&e));
    CHECK_EQ(e.nib, end1 + 6);
    CHECK_EQ(nib(&e, end1), 0x7); CHECK_EQ(nib(&e, end1 + 1), 0x4);
    uint16_t got1 = (uint16_t)((nib(&e, end1 + 2) << 12) | (nib(&e, end1 + 3) << 8) | (nib(&e, end1 + 4) << 4) | nib(&e, end1 + 5));
    CHECK_EQ(got1, want1);

    CHECK(!enc_seal(&e));                                     /* nothing new: no second CHECK */
    CHECK_EQ(e.nib, end1 + 6);

    unsigned start2 = e.nib;
    enc_put_sample(&e, 2150); enc_put_error(&e); enc_put_sample(&e, 2100);
    unsigned end2 = e.nib;
    uint16_t want2 = crc_ref(&e, start2, end2);               /* only what came after the first CHECK */
    CHECK(enc_seal(&e));
    uint16_t got2 = (uint16_t)((nib(&e, end2 + 2) << 12) | (nib(&e, end2 + 3) << 8) | (nib(&e, end2 + 4) << 4) | nib(&e, end2 + 5));
    CHECK_EQ(got2, want2);
}

int main(void) {
    RUN(test_crc_reference_check_value);
    RUN(test_temp_to_q);
    RUN(test_reset_page);
    RUN(test_start_record_bytes);
    RUN(test_start_without_boot);
    RUN(test_interval_limits);
    RUN(test_delta_boundaries);
    RUN(test_sample_before_anchor_is_anchor);
    RUN(test_error_record_keeps_value);
    RUN(test_round_trip_no_drift);
    RUN(test_page_full_keeps_reserve);
    RUN(test_seal_crc_covers_only_new_data);
    TESTS_DONE();
}
