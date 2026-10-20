#ifndef TEMP_ENCODER_H
#define TEMP_ENCODER_H
#include "temp_decoder.h"
#define TEMP_STREAM_CAPACITY 100u
#define TEMP_BLOB_CAPACITY (2u + TEMP_STREAM_CAPACITY)
typedef struct {
    uint8_t buffer[TEMP_STREAM_CAPACITY];
    size_t nibbles;
    uint16_t sample_count;
    bool has_anchor;
    int16_t last_anchor;
} temp_encoder_t;
void temp_encoder_clear(temp_encoder_t *encoder);
// Returns false without changing state if the complete sample cannot fit.
bool temp_encoder_store(temp_encoder_t *encoder, int16_t temperature,
                        temp_decoded_sample_t *encoding);
size_t temp_encoder_length(const temp_encoder_t *encoder);
// Copies a stable application blob; zero samples produce zero bytes.
bool temp_encoder_snapshot(const temp_encoder_t *encoder, uint8_t *output,
                           size_t capacity, size_t *length);
#endif
