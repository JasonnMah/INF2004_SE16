#ifndef TEMP_DECODER_H
#define TEMP_DECODER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    TEMP_DECODE_OK,
    TEMP_DECODE_INVALID_ARGUMENT,
    TEMP_DECODE_TRUNCATED,
    TEMP_DECODE_NO_ANCHOR,
    TEMP_DECODE_OVERFLOW
} temp_decode_status_t;

typedef struct {
    uint16_t index; // Zero-based sample index.
    int16_t temperature_x100;
    bool is_anchor;
    int8_t steps; // Meaningful for deltas only.
} temp_decoded_sample_t;

typedef void (*temp_sample_fn)(const temp_decoded_sample_t *sample, void *context);

// Blob: uint16 LE sample count, then high-nibble-first compressed stream.
// Zero bytes means empty; NULL data is allowed only for that case.
// Validates the entire declared log before invoking callbacks (no partial table).
// A NULL callback performs validation only. No dynamic allocation.
// The caller must keep the input immutable throughout validation and callbacks.
// Stops at the declared count; trailing padding is not decoded as samples.
temp_decode_status_t temp_decode(const uint8_t *data, size_t length,
                                 temp_sample_fn emit, void *context,
                                 uint16_t *sample_count);
const char *temp_decode_status_string(temp_decode_status_t status);
#endif
