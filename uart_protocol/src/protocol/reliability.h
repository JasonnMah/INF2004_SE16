#ifndef PROTOCOL_RELIABILITY_H
#define PROTOCOL_RELIABILITY_H

#include "link.h"

#define ACK_TIMEOUT_MS 500
#define MAX_RETRIES 3

typedef struct
{
    bool active;
    uint16_t sequence;
    uint8_t frame[MAX_STUFFED_FRAME_SIZE];
    size_t frame_length;
    uint64_t sent_at_ms;
    uint8_t retries;

} pending_tx_t;

// Prepare without transmitting so an optional demo hook can alter the first copy.
bool prepare_tracked_frame(
    uint8_t message_type,
    uint32_t session_id,
    uint16_t sequence,
    const uint8_t *payload,
    uint16_t payload_length,
    pending_tx_t *pending);
bool send_tracked_frame(
    uart_inst_t *uart,
    uint8_t message_type,
    uint32_t session_id,
    uint16_t sequence,
    const uint8_t *payload,
    uint16_t payload_length,
    pending_tx_t *pending);
bool retry_pending(
    uart_inst_t *uart,
    pending_tx_t *pending);
bool send_ack(
    uart_inst_t *uart,
    uint32_t session_id,
    uint16_t tx_sequence,
    uint16_t acknowledged_sequence,
    uint8_t *scratch,
    size_t scratch_size);
bool send_nack(
    uart_inst_t *uart,
    uint32_t session_id,
    uint16_t tx_sequence,
    uint16_t rejected_sequence,
    uint8_t reason,
    uint8_t *scratch,
    size_t scratch_size);
bool pending_timed_out(const pending_tx_t *pending);
#endif
