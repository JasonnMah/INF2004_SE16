#ifndef PROTOCOL_FRAME_H
#define PROTOCOL_FRAME_H

#include "protocol.h"

uint16_t read_u16_le(const uint8_t *p);
uint32_t read_u32_le(const uint8_t *p);
void write_u16_le(uint8_t *p, uint16_t value);
void write_u32_le(uint8_t *p, uint32_t value);
size_t stuff_frame(
    const uint8_t *input,
    size_t input_length,
    uint8_t *output,
    size_t output_size);
size_t unstuff_frame(
    const uint8_t *input,
    size_t input_length,
    uint8_t *output,
    size_t output_size);
size_t frame_encode(
    uint8_t message_type,
    uint8_t flags,
    uint32_t session_id,
    uint16_t sequence,
    const uint8_t *payload,
    uint16_t payload_length,
    uint8_t *output,
    size_t output_size);
bool frame_decode(
    const uint8_t *stuffed_input,
    size_t stuffed_length,
    protocol_frame_t *frame);
#endif
