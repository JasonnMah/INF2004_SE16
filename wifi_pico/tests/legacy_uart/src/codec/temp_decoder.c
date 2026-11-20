#include "temp_decoder.h"
#include <limits.h>

typedef struct {
    const uint8_t *data;
    size_t length;
    size_t byte;
    bool low;
} nibble_reader_t;

static bool next_nibble(nibble_reader_t *r, uint8_t *value)
{
    if (r->byte >= r->length) { return false; }
    *value = r->low ? (r->data[r->byte] & 0x0fu) : (r->data[r->byte] >> 4);
    if (r->low) { r->byte++; }
    r->low = !r->low;
    return true;
}

static temp_decode_status_t decode_samples(const uint8_t *data, size_t length,
    uint16_t count, temp_sample_fn emit, void *context)
{
    nibble_reader_t reader = {.data = data, .length = length, .byte = 2};
    bool have_anchor = false;
    int32_t anchor = 0;
    for (uint32_t i = 0; i < count; i++)
    {
        uint8_t nibble;
        if (!next_nibble(&reader, &nibble)) { return TEMP_DECODE_TRUNCATED; }
        temp_decoded_sample_t sample = {.index = (uint16_t)i};
        int32_t temperature;
        if (nibble == 8)
        {
            uint16_t bits = 0;
            for (unsigned j = 0; j < 4; j++)
            {
                if (!next_nibble(&reader, &nibble)) { return TEMP_DECODE_TRUNCATED; }
                bits = (uint16_t)((bits << 4) | nibble);
            }
            // Explicit sign reconstruction avoids implementation-defined casts.
            anchor = bits >= 0x8000u ? (int32_t)bits - 65536 : bits;
            have_anchor = true;
            temperature = anchor;
            sample.is_anchor = true;
        }
        else
        {
            if (!have_anchor) { return TEMP_DECODE_NO_ANCHOR; }
            sample.steps = (int8_t)(nibble <= 7 ? (int)nibble : (int)nibble - 16);
            // Deltas are anchor-relative, never cumulative between samples.
            temperature = anchor + (int32_t)sample.steps * 25;
        }
        if (temperature < INT16_MIN || temperature > INT16_MAX)
        {
            return TEMP_DECODE_OVERFLOW;
        }
        sample.temperature_x100 = (int16_t)temperature;
        if (emit != NULL) { emit(&sample, context); }
    }
    return TEMP_DECODE_OK;
}

temp_decode_status_t temp_decode(const uint8_t *data, size_t length,
    temp_sample_fn emit, void *context, uint16_t *sample_count)
{
    if (sample_count == NULL) { return TEMP_DECODE_INVALID_ARGUMENT; }
    *sample_count = 0;
    if (length == 0) { return TEMP_DECODE_OK; }
    if (data == NULL) { return TEMP_DECODE_INVALID_ARGUMENT; }
    if (length < 2) { return TEMP_DECODE_TRUNCATED; }
    uint16_t count = (uint16_t)(data[0] | ((uint16_t)data[1] << 8));
    temp_decode_status_t status = decode_samples(data, length, count, NULL, NULL);
    if (status != TEMP_DECODE_OK) { return status; }
    if (emit != NULL)
    {
        status = decode_samples(data, length, count, emit, context);
    }
    if (status == TEMP_DECODE_OK) { *sample_count = count; }
    return status;
}

const char *temp_decode_status_string(temp_decode_status_t status)
{
    switch (status)
    {
        case TEMP_DECODE_OK: return "OK";
        case TEMP_DECODE_INVALID_ARGUMENT: return "invalid argument";
        case TEMP_DECODE_TRUNCATED: return "sample count or anchor exceeds available data";
        case TEMP_DECODE_NO_ANCHOR: return "delta before first anchor";
        case TEMP_DECODE_OVERFLOW: return "temperature exceeds int16 range";
        default: return "unknown decoder status";
    }
}
