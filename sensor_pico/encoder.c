#include "encoder.h"
#include <string.h>

#define NIB_META       0x7
#define NIB_ESCAPE     0x8
#define META_TIMEBASE  0x1
#define META_SENSOR_ERR 0x2
#define META_BOOT      0x3
#define META_CHECK     0x4

static void set_nib(enc_t *e, unsigned i, uint8_t v) {
    unsigned b = i >> 1;
    if (i & 1) e->page[b] = (uint8_t)((e->page[b] & 0xF0) | (v & 0x0F));
    else       e->page[b] = (uint8_t)(((v & 0x0F) << 4) | (e->page[b] & 0x0F));
}

static uint8_t get_nib(const enc_t *e, unsigned i) {
    return (i & 1) ? (uint8_t)(e->page[i >> 1] & 0x0F) : (uint8_t)(e->page[i >> 1] >> 4);
}

static void put_nib(enc_t *e, uint8_t v) {
    set_nib(e, e->nib, v);
    e->nib++;
}

/* Low `n` nibbles of value, most significant first. */
static void put_field(enc_t *e, uint32_t value, unsigned n) {
    for (int i = (int)n - 1; i >= 0; i--)
        put_nib(e, (uint8_t)((value >> (4 * i)) & 0x0F));
}

static bool fits(const enc_t *e, unsigned len) {
    return (unsigned)e->nib + len + ENC_CHECK_NIBBLES <= ENC_PAGE_NIBBLES;
}

/* CRC-16: poly 0x1021, init 0xFFFF, no reflection, no final XOR (CCITT-FALSE), fed one nibble
 * at a time, most significant bit first. Check value for "123456789" is 0x29B1. */
static uint16_t crc_nibbles(const enc_t *e, unsigned from, unsigned to) {
    uint16_t crc = 0xFFFF;
    for (unsigned i = from; i < to; i++) {
        crc ^= (uint16_t)((uint16_t)get_nib(e, i) << 12);
        for (int b = 0; b < 4; b++)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

void enc_reset_page(enc_t *e) {
    memset(e->page, 0xFF, sizeof e->page);
    e->nib = 0;
    e->chunk_start = 0;
    e->last_q = 0;
    e->has_anchor = false;
}

int16_t enc_temp_to_q(int16_t t_centi) {
    int32_t x = (int32_t)t_centi + 4000;          /* offset from -40.00 C */
    if (x <= 0) return 0;
    int32_t q = (x + 12) / 25;                    /* round to the nearest 0.25 C */
    return q > 500 ? 500 : (int16_t)q;
}

static uint8_t delta_nibble(int d) {
    return d >= 0 ? (uint8_t)d : (uint8_t)(0x8 | -d);
}

enc_result_t enc_put_start(enc_t *e, int16_t t_centi, uint32_t unix_time,
                           uint16_t interval_min, bool boot) {
    if (interval_min == 0) return ENC_BAD_ARG;
    unsigned len = (boot ? 2u : 0u) + 18u;        /* [7 3] 7 1, 8 time, 4 interval, 8 q(3) */
    if (!fits(e, len)) return ENC_NO_ROOM;

    int16_t q = enc_temp_to_q(t_centi);
    if (boot) { put_nib(e, NIB_META); put_nib(e, META_BOOT); }
    put_nib(e, NIB_META);
    put_nib(e, META_TIMEBASE);
    put_field(e, unix_time, 8);
    put_field(e, interval_min, 4);
    put_nib(e, NIB_ESCAPE);
    put_field(e, (uint32_t)q, 3);
    e->last_q = q;
    e->has_anchor = true;
    return ENC_OK;
}

enc_result_t enc_put_sample(enc_t *e, int16_t t_centi) {
    int16_t q = enc_temp_to_q(t_centi);
    bool use_delta = false;
    int d = 0;
    if (e->has_anchor) {
        d = q - e->last_q;
        use_delta = (d >= -ENC_MAX_DELTA && d <= ENC_MAX_DELTA);
    }
    if (!fits(e, use_delta ? 1u : 4u)) return ENC_NO_ROOM;

    if (use_delta) {
        put_nib(e, delta_nibble(d));
    } else {
        put_nib(e, NIB_ESCAPE);
        put_field(e, (uint32_t)q, 3);
        e->has_anchor = true;
    }
    e->last_q = q;                                /* track the decoded value: no drift */
    return ENC_OK;
}

enc_result_t enc_put_error(enc_t *e) {
    if (!fits(e, 2)) return ENC_NO_ROOM;
    put_nib(e, NIB_META);
    put_nib(e, META_SENSOR_ERR);
    return ENC_OK;
}

bool enc_has_unsealed(const enc_t *e) {
    return e->nib != e->chunk_start;
}

bool enc_seal(enc_t *e) {
    if (!enc_has_unsealed(e)) return false;
    if ((unsigned)e->nib + ENC_CHECK_NIBBLES > ENC_PAGE_NIBBLES) return false;   /* cannot happen */
    uint16_t crc = crc_nibbles(e, e->chunk_start, e->nib);
    put_nib(e, NIB_META);
    put_nib(e, META_CHECK);
    put_field(e, crc, 4);
    e->chunk_start = e->nib;
    return true;
}
