#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "codec/temp_decoder.h"
#include "codec/temp_encoder.h"
#include "codec/temp_log_print.h"

static temp_decoded_sample_t samples[128];
static unsigned emitted;
static void collect(const temp_decoded_sample_t *sample, void *context)
{
    (void)context;
    assert(emitted < 128 && sample->index == emitted);
    samples[emitted++] = *sample;
}
static temp_decode_status_t decode(const uint8_t *data, size_t length, uint16_t expected)
{
    uint16_t count = 999;
    emitted = 0;
    temp_decode_status_t status = temp_decode(data, length, collect, NULL, &count);
    if (status == TEMP_DECODE_OK) { assert(count == expected && emitted == expected); }
    else { assert(count == 0 && emitted == 0); }
    return status;
}

// Test-only reference packer for independent vectors; actual encoder tested below.
static uint8_t blob[256];
static size_t nibbles;
static uint16_t encoded;
static int anchor;
static void nibble(int value)
{
    uint8_t n = ((uint8_t)value) & 0x0f;
    size_t byte = 2 + nibbles / 2;
    if ((nibbles & 1) == 0) { blob[byte] = (uint8_t)(n << 4); }
    else { blob[byte] |= n; }
    nibbles++;
}
static void encode(int16_t temperature)
{
    int steps = ((int)temperature - anchor) / 25;
    if (encoded == 0 || steps < -7 || steps > 7)
    {
        uint16_t bits = (uint16_t)temperature;
        nibble(8);
        for (int shift = 12; shift >= 0; shift -= 4) { nibble(bits >> shift); }
        anchor = temperature;
    }
    else { nibble(steps); }
    encoded++;
    blob[0] = (uint8_t)encoded; blob[1] = (uint8_t)(encoded >> 8);
}
static void reset(void)
{
    memset(blob, 0xaa, sizeof(blob)); // First nibble must overwrite stale contents.
    nibbles = encoded = 0; anchor = 0;
}
static void vectors(void)
{
    const uint8_t positive[] = {1, 0, 0x80, 0x96, 0x0f};
    assert(decode(positive, sizeof(positive), 1) == TEMP_DECODE_OK);
    assert(samples[0].temperature_x100 == 2400 && samples[0].is_anchor);
    const uint8_t negative[] = {1, 0, 0x8f, 0xfb, 0x50}; // -75 hundredths
    assert(decode(negative, sizeof(negative), 1) == TEMP_DECODE_OK);
    assert(samples[0].temperature_x100 == -75);
    reset(); encode(2400); encode(2425); encode(2375);
    assert(decode(blob, 2 + (nibbles + 1) / 2, 3) == TEMP_DECODE_OK);
    assert(samples[1].temperature_x100 == 2425 && samples[2].temperature_x100 == 2375);
    // Anchor is five nibbles: +1 is initially LOW, -1 is then HIGH.
    // Add another -1 to explicitly exercise a negative LOW nibble as requested.
    encode(2375); assert(blob[5] == 0xff);
    assert(decode(blob, 2 + (nibbles + 1) / 2, 4) == TEMP_DECODE_OK);
    assert(samples[3].steps == -1);
    reset(); encode(2400); encode(2400); encode(2425); encode(2375);
    assert(blob[5] == 0x1f); // Critical +1 HIGH / -1 LOW preservation.
    assert(decode(blob, 6, 4) == TEMP_DECODE_OK);
    assert(samples[2].temperature_x100 == 2425 && samples[3].temperature_x100 == 2375);
    reset(); encode(-1000);
    for (int step = -7; step <= 7; step++) { encode((int16_t)(-1000 + step * 25)); }
    assert(decode(blob, 2 + (nibbles + 1) / 2, 16) == TEMP_DECODE_OK);
    for (int step = -7; step <= 7; step++)
    {
        assert(samples[step + 8].steps == step);
        assert(samples[step + 8].temperature_x100 == -1000 + step * 25);
    }
    encode(3000); encode(3025); encode(-2000);
    assert(decode(blob, 2 + (nibbles + 1) / 2, 19) == TEMP_DECODE_OK);
    assert(samples[16].is_anchor && samples[16].temperature_x100 == 3000);
    assert(samples[18].is_anchor && samples[18].temperature_x100 == -2000);
}
static void malformed(void)
{
    uint16_t count;
    assert(decode(NULL, 0, 0) == TEMP_DECODE_OK);
    const uint8_t empty[] = {0, 0};
    assert(decode(empty, 2, 0) == TEMP_DECODE_OK);
    assert(temp_decode(NULL, 1, NULL, NULL, &count) == TEMP_DECODE_INVALID_ARGUMENT);
    assert(temp_decode(empty, 2, NULL, NULL, NULL) == TEMP_DECODE_INVALID_ARGUMENT);
    assert(decode(empty, 1, 0) == TEMP_DECODE_TRUNCATED);
    const uint8_t delta[] = {1, 0, 0x10};
    assert(decode(delta, sizeof(delta), 0) == TEMP_DECODE_NO_ANCHOR);
    const uint8_t truncated[] = {1, 0, 0x80, 0x96};
    assert(decode(truncated, sizeof(truncated), 0) == TEMP_DECODE_TRUNCATED);
    const uint8_t excess[] = {3, 0, 0x80, 0x96, 0x00};
    assert(decode(excess, sizeof(excess), 0) == TEMP_DECODE_TRUNCATED);
    const uint8_t overflow[] = {2, 0, 0x87, 0xff, 0xf1};
    assert(decode(overflow, sizeof(overflow), 0) == TEMP_DECODE_OVERFLOW);
    const uint8_t underflow[] = {2, 0, 0x88, 0x00, 0x0f};
    assert(decode(underflow, sizeof(underflow), 0) == TEMP_DECODE_OVERFLOW);
}
static void production_encoder(void)
{
    temp_encoder_t e;
    temp_encoder_clear(&e);
    int16_t values[] = {2400, 2400, 2425, 2375, 3000, -1000, -975};
    temp_decoded_sample_t result;
    for (size_t i = 0; i < sizeof(values)/sizeof(values[0]); i++)
    {
        assert(temp_encoder_store(&e, values[i], &result));
        assert(result.index == i);
        if (i == 4 || i == 5) { assert(result.is_anchor); }
    }
    assert(e.buffer[3] == 0x1f); // Actual extracted encoder: negative LOW nibble.
    uint8_t snapshot[TEMP_BLOB_CAPACITY]; size_t length;
    assert(temp_encoder_snapshot(&e, snapshot, sizeof(snapshot), &length));
    assert(snapshot[0] == 7 && snapshot[1] == 0);
    assert(decode(snapshot, length, 7) == TEMP_DECODE_OK);
    for (unsigned i = 0; i < 7; i++) { assert(samples[i].temperature_x100 == values[i]); }
    uint8_t saved[TEMP_BLOB_CAPACITY]; memcpy(saved, snapshot, length);
    assert(temp_encoder_store(&e, -950, NULL));
    assert(memcmp(saved, snapshot, length) == 0); // Acquisition cannot mutate snapshot.
    temp_encoder_clear(&e);
    assert(e.sample_count == 0 && e.nibbles == 0 && !e.has_anchor);
    assert(temp_encoder_snapshot(&e, snapshot, sizeof(snapshot), &length) && length == 0);
    assert(temp_encoder_store(&e, 2400, NULL));
    assert(temp_encoder_store(&e, 2412, &result) && result.temperature_x100 == 2400);
    assert(temp_encoder_store(&e, 2413, &result) && result.temperature_x100 == 2425);
    assert(temp_encoder_store(&e, 2387, &result) && result.temperature_x100 == 2375);
    // Failed full-anchor reservation must leave every byte/counter unchanged.
    for (size_t remaining = 0; remaining < 5; remaining++)
    {
        temp_encoder_clear(&e);
        assert(temp_encoder_store(&e, 0, NULL));
        while (e.nibbles < 200 - remaining) { assert(temp_encoder_store(&e, 0, NULL)); }
        temp_encoder_t before = e;
        assert(!temp_encoder_store(&e, 1000, NULL));
        assert(memcmp(&before, &e, sizeof(e)) == 0);
    }
    temp_encoder_clear(&e);
    assert(temp_encoder_store(&e, INT16_MIN, NULL));
    assert(temp_encoder_store(&e, INT16_MAX, NULL)); // Difference must not wrap int16.
    assert(temp_encoder_snapshot(&e, snapshot, sizeof(snapshot), &length));
    assert(decode(snapshot, length, 2) == TEMP_DECODE_OK);
    assert(samples[0].temperature_x100 == INT16_MIN && samples[1].temperature_x100 == INT16_MAX);
    assert(!temp_encoder_snapshot(&e, snapshot, 2, &length));
    // Print a deterministic example to exercise formatting, including negatives.
    temp_encoder_clear(&e);
    int16_t example[] = {2400, 2425, 2400, 2375};
    for (unsigned i = 0; i < 4; i++)
    {
        assert(temp_encoder_store(&e, example[i], &result)); temp_encode_print(example[i], &result);
    }
    assert(temp_encoder_snapshot(&e, snapshot, sizeof(snapshot), &length));
    temp_log_print(snapshot, length, 1700000000u, 3600u);
    temp_log_print(NULL, 0, 0, 0);
}

int main(void)
{
    vectors(); malformed(); production_encoder();
    puts("PASS decoder: signed anchors, all deltas, anchor-relative round trips, padding/count, malformed input and overflow");
}
