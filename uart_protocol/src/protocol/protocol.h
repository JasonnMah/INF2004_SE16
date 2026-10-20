#ifndef PROTOCOL_PROTOCOL_H
#define PROTOCOL_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define SOF              0x7E
#define ESCAPE           0x7D
#define PROTOCOL_VERSION 0x01

#define MAX_PAYLOAD_SIZE 256
#define FIXED_FRAME_OVERHEAD 15
#define MAX_LOGICAL_FRAME_SIZE (FIXED_FRAME_OVERHEAD + MAX_PAYLOAD_SIZE)
#define MAX_STUFFED_FRAME_SIZE (2 + (2 * (MAX_LOGICAL_FRAME_SIZE - 2)))
#define TX_BUFFER_SIZE MAX_STUFFED_FRAME_SIZE

typedef enum
{
    MSG_HELLO = 0x01,
    MSG_CONFIG = 0x02,
    MSG_REQUEST_DATA = 0x03,
    MSG_DATA = 0x04,
    MSG_ACK = 0x05,
    MSG_NACK = 0x06,
    MSG_CLEAR_LOG = 0x07,
    MSG_TRANSFER_END = 0x08
} protocol_message_t;

// Payload sizes on the wire.

#define CONFIG_PAYLOAD_SIZE      8
#define ACK_PAYLOAD_SIZE         2
#define NACK_PAYLOAD_SIZE        3
#define REQUEST_DATA_PAYLOAD_SIZE 2
#define TRANSFER_END_PAYLOAD_SIZE 2

// DATA payload = 2-byte block index + compressed bytes.
#define DATA_CHUNK_SIZE 8


#define DATA_BLOCK_INDEX_SIZE 2
#define CONFIG_INTERVAL_OFFSET 4
#define FRAME_HEADER_SIZE 11
#define FRAME_VERSION_OFFSET 1
#define FRAME_TYPE_OFFSET 2
#define FRAME_FLAGS_OFFSET 3
#define FRAME_SESSION_OFFSET 4
#define FRAME_SEQUENCE_OFFSET 8
#define FRAME_LENGTH_OFFSET 10
#define FRAME_PAYLOAD_OFFSET 12

typedef enum
{
    NACK_BAD_PAYLOAD = 0x01,
    NACK_UNEXPECTED_MSG = 0x02,
    NACK_BAD_BLOCK = 0x03
} protocol_nack_reason_t;

_Static_assert(DATA_BLOCK_INDEX_SIZE + DATA_CHUNK_SIZE <= MAX_PAYLOAD_SIZE,
               "DATA chunk must fit one frame payload");

typedef struct
{
    uint8_t version;
    uint8_t message_type;
    uint8_t flags;

    uint32_t session_id;
    uint16_t sequence;

    uint16_t payload_length;
    uint8_t payload[MAX_PAYLOAD_SIZE];

} protocol_frame_t;


#endif
