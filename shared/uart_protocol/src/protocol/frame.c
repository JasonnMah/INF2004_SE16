#include "frame.h"
#include "crc16.h"
#include <string.h>

uint16_t read_u16_le(const uint8_t *p)
{
    return (uint16_t)p[0] |
           ((uint16_t)p[1] << 8);
}

uint32_t read_u32_le(const uint8_t *p)
{
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

void write_u16_le(uint8_t *p, uint16_t value)
{
    p[0] = value & 0xFF;
    p[1] = (value >> 8) & 0xFF;
}

void write_u32_le(uint8_t *p, uint32_t value)
{
    p[0] = value & 0xFF;
    p[1] = (value >> 8) & 0xFF;
    p[2] = (value >> 16) & 0xFF;
    p[3] = (value >> 24) & 0xFF;
}

size_t stuff_frame(
    const uint8_t *input,
    size_t input_length,
    uint8_t *output,
    size_t output_size)
{
    size_t out = 0;

    if (input_length < 2 || output_size < 2)
    {
        return 0;
    }

    output[out++] = input[0];

    for (size_t i = 1; i < input_length - 1; i++)
    {
        if (input[i] == SOF)
        {
            if (out + 2 > output_size)
            {
                return 0;
            }

            output[out++] = ESCAPE;
            output[out++] = 0x5E;
        }
        else if (input[i] == ESCAPE)
        {
            if (out + 2 > output_size)
            {
                return 0;
            }

            output[out++] = ESCAPE;
            output[out++] = 0x5D;
        }
        else
        {
            if (out + 1 > output_size)
            {
                return 0;
            }

            output[out++] = input[i];
        }
    }

    if (out >= output_size)
    {
        return 0;
    }

    output[out++] = input[input_length - 1];

    return out;
}

size_t unstuff_frame(
    const uint8_t *input,
    size_t input_length,
    uint8_t *output,
    size_t output_size)
{
    size_t out = 0;

    if (input_length < 2 || output_size < 2)
    {
        return 0;
    }

    output[out++] = input[0];

    for (size_t i = 1; i < input_length - 1; i++)
    {
        if (input[i] == ESCAPE)
        {
            if (i + 1 >= input_length - 1)
            {
                return 0;
            }

            i++;

            if (out >= output_size)
            {
                return 0;
            }

            if (input[i] == 0x5E)
            {
                output[out++] = SOF;
            }
            else if (input[i] == 0x5D)
            {
                output[out++] = ESCAPE;
            }
            else
            {
                return 0;
            }
        }
        else
        {
            if (out >= output_size)
            {
                return 0;
            }

            output[out++] = input[i];
        }
    }

    if (out >= output_size)
    {
        return 0;
    }

    output[out++] = input[input_length - 1];

    return out;
}

size_t frame_encode(
    uint8_t message_type,
    uint8_t flags,
    uint32_t session_id,
    uint16_t sequence,
    const uint8_t *payload,
    uint16_t payload_length,
    uint8_t *output,
    size_t output_size)
{
    if (payload_length > MAX_PAYLOAD_SIZE)
    {
        return 0;
    }

    if (payload_length > 0 && payload == NULL)
    {
        return 0;
    }

    uint8_t logical_frame[MAX_LOGICAL_FRAME_SIZE];
    size_t index = 0;

    logical_frame[index++] = SOF;
    logical_frame[index++] = PROTOCOL_VERSION;
    logical_frame[index++] = message_type;
    logical_frame[index++] = flags;

    write_u32_le(&logical_frame[index], session_id);
    index += 4;

    write_u16_le(&logical_frame[index], sequence);
    index += 2;

    write_u16_le(&logical_frame[index], payload_length);
    index += 2;

    if (payload_length > 0)
    {
        memcpy(&logical_frame[index], payload, payload_length);
        index += payload_length;
    }

    uint16_t crc = crc16_x25(
        &logical_frame[FRAME_VERSION_OFFSET],
        FRAME_HEADER_SIZE + payload_length
    );

    write_u16_le(&logical_frame[index], crc);
    index += 2;

    logical_frame[index++] = SOF;

    return stuff_frame(
        logical_frame,
        index,
        output,
        output_size
    );
}

bool frame_decode(
    const uint8_t *stuffed_input,
    size_t stuffed_length,
    protocol_frame_t *frame)
{
    uint8_t logical_frame[MAX_LOGICAL_FRAME_SIZE];

    size_t logical_length = unstuff_frame(
        stuffed_input,
        stuffed_length,
        logical_frame,
        sizeof(logical_frame)
    );

    if (logical_length < FIXED_FRAME_OVERHEAD)
    {
        return false;
    }

    if (logical_frame[0] != SOF ||
        logical_frame[logical_length - 1] != SOF)
    {
        return false;
    }

    if (logical_frame[FRAME_VERSION_OFFSET] != PROTOCOL_VERSION)
    {
        return false;
    }

    uint16_t payload_length = read_u16_le(&logical_frame[FRAME_LENGTH_OFFSET]);

    if (payload_length > MAX_PAYLOAD_SIZE)
    {
        return false;
    }

    size_t expected_length =
        FIXED_FRAME_OVERHEAD + payload_length;

    if (logical_length != expected_length)
    {
        return false;
    }

    size_t crc_index = FRAME_PAYLOAD_OFFSET + payload_length;

    uint16_t received_crc = read_u16_le(&logical_frame[crc_index]);

    uint16_t calculated_crc = crc16_x25(
        &logical_frame[FRAME_VERSION_OFFSET],
        FRAME_HEADER_SIZE + payload_length
    );

    if (received_crc != calculated_crc)
    {
        return false;
    }

    frame->version = logical_frame[FRAME_VERSION_OFFSET];
    frame->message_type = logical_frame[FRAME_TYPE_OFFSET];
    frame->flags = logical_frame[FRAME_FLAGS_OFFSET];
    frame->session_id = read_u32_le(&logical_frame[FRAME_SESSION_OFFSET]);
    frame->sequence = read_u16_le(&logical_frame[FRAME_SEQUENCE_OFFSET]);
    frame->payload_length = payload_length;

    memcpy(frame->payload, &logical_frame[FRAME_PAYLOAD_OFFSET], payload_length);

    return true;
}
