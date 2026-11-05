/*
 * encoder.h - nibble-stream encoder for the temperature log (no SDK dependencies).
 *
 * The encoder fills a 256-byte RAM image of ONE flash page. It never touches flash.
 *
 * Nibble codes:  0x0-0x6 delta +0..+6 steps      0x9-0xE delta -1..-6 steps (0x8 | magnitude)
 *                0x7 metadata prefix             0x8 escape, followed by a 3-nibble anchor q
 *                0xF empty / end of data
 * Metadata:      7 1 TIMEBASE (32-bit UTC, 12-bit interval in minutes; always followed by an anchor)
 *                7 2 SENSOR_ERR    7 3 BOOT    7 4 CHECK (CRC-16 over the nibbles since the last CHECK)
 * q = (T + 40 C) / 0.25 C, clamped to 0..500. Nibbles are packed high nibble first.
 *
 * Every put function returns ENC_NO_ROOM, without changing anything, if the unit plus the
 * 6-nibble CHECK reserve does not fit. The caller then seals, programs and advances the page,
 * calls enc_reset_page(), and retries.
 */
#ifndef ENCODER_H
#define ENCODER_H

#include <stdint.h>
#include <stdbool.h>

#define ENC_PAGE_BYTES    256u
#define ENC_PAGE_NIBBLES  (ENC_PAGE_BYTES * 2u)
#define ENC_CHECK_NIBBLES 6u          /* 7, 4 and a 4-nibble CRC: always kept in reserve */
#define ENC_INTERVAL_NIBBLES 3u
#define ENC_MAX_INTERVAL_MINUTES 4095u
#define ENC_MAX_DELTA     6

typedef enum {
    ENC_OK = 0,
    ENC_NO_ROOM,                      /* does not fit in this page */
    ENC_BAD_ARG                       /* interval must be 1..4095 whole minutes */
} enc_result_t;

typedef struct {
    uint8_t  page[ENC_PAGE_BYTES];    /* 0xFF = empty */
    uint16_t nib;                     /* nibbles written so far */
    uint16_t chunk_start;             /* first nibble not yet covered by a CHECK */
    int16_t  last_q;                  /* value the decoder holds after the last sample */
    bool     has_anchor;              /* last_q is valid, so deltas are allowed */
} enc_t;

/* Start a new page: image all 0xFF, no anchor, so the next sample is written as an anchor. */
void enc_reset_page(enc_t *e);

/* Temperature in 0.01 C to q (0.25 C steps from -40 C). */
int16_t enc_temp_to_q(int16_t t_centi);

/* [BOOT] + TIMEBASE + anchor for the sample taken now.
 * New pages use a 3-nibble interval. Do not append them to legacy 4-nibble logs. */
enc_result_t enc_put_start(enc_t *e, int16_t t_centi, uint32_t unix_time,
                           uint16_t interval_min, bool boot);

/* One temperature sample: a delta if possible, otherwise an anchor. */
enc_result_t enc_put_sample(enc_t *e, int16_t t_centi);

/* One sample slot with no value (failed read). The tracked value is unchanged. */
enc_result_t enc_put_error(enc_t *e);

/* Append a CHECK over everything since the previous CHECK. Returns false if there was
 * nothing new to cover. Always fits, thanks to the reserve. */
bool enc_seal(enc_t *e);

/* Has data been written since the last CHECK? */
bool enc_has_unsealed(const enc_t *e);

#endif
