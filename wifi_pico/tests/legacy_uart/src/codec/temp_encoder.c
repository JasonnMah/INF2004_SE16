// Extracted from Member 1's src/apps/main.c; anchor-relative rounding preserved.
#include "temp_encoder.h"
#include <limits.h>
#include <string.h>

static void store_step(temp_encoder_t *e, int steps)
{
    uint8_t nibble = ((uint8_t)steps) & 0x0f;
    size_t byte = e->nibbles / 2;
    if ((e->nibbles & 1u) == 0) { e->buffer[byte] = (uint8_t)(nibble << 4); }
    else { e->buffer[byte] |= nibble; }
    e->nibbles++;
}
static void store_anchor_t(temp_encoder_t *e, int16_t temperature)
{
    uint16_t bits = (uint16_t)temperature;
    store_step(e, 8);
    for (int shift = 12; shift >= 0; shift -= 4) { store_step(e, bits >> shift); }
}
void temp_encoder_clear(temp_encoder_t *e)
{
    if (e != NULL) { *e = (temp_encoder_t){0}; }
}
bool temp_encoder_store(temp_encoder_t *e, int16_t temperature,
                        temp_decoded_sample_t *encoding)
{
    if (e == NULL || e->sample_count == UINT16_MAX) { return false; }
    int32_t diff = (int32_t)temperature - e->last_anchor;
    int32_t steps = (diff >= 0 ? diff + 12 : diff - 12) / 25;
    int32_t reconstructed = (int32_t)e->last_anchor + steps * 25;
    bool anchor = !e->has_anchor || steps < -7 || steps > 7 ||
                  reconstructed < INT16_MIN || reconstructed > INT16_MAX;
    size_t needed = anchor ? 5 : 1;
    // Reserve the entire anchor before writing anything, including the escape.
    if (e->nibbles > TEMP_STREAM_CAPACITY * 2u ||
        needed > TEMP_STREAM_CAPACITY * 2u - e->nibbles) { return false; }
    if (anchor)
    {
        store_anchor_t(e, temperature);
        e->last_anchor = temperature;
        e->has_anchor = true;
        reconstructed = temperature;
    }
    else { store_step(e, (int)steps); }
    if (encoding != NULL)
    {
        *encoding = (temp_decoded_sample_t){.index = e->sample_count,
            .temperature_x100 = (int16_t)reconstructed, .is_anchor = anchor,
            .steps = anchor ? 0 : (int8_t)steps};
    }
    e->sample_count++;
    return true;
}
size_t temp_encoder_length(const temp_encoder_t *e)
{
    return (e->nibbles + 1u) / 2u;
}
bool temp_encoder_snapshot(const temp_encoder_t *e, uint8_t *output,
                           size_t capacity, size_t *length)
{
    if (e == NULL || length == NULL) { return false; }
    *length = 0;
    if (e->sample_count == 0) { return true; }
    size_t bytes = temp_encoder_length(e);
    if (output == NULL || capacity < 2 || bytes > capacity - 2) { return false; }
    output[0] = (uint8_t)e->sample_count;
    output[1] = (uint8_t)(e->sample_count >> 8);
    memcpy(output + 2, e->buffer, bytes);
    *length = bytes + 2;
    return true;
}
