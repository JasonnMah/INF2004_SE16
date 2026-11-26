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
    uint32_t timeout_ms; /* Default 500 ms; explicit long storage command policy only. */

} pending_tx_t;

// Prepare without transmitting so an optional demo hook can alter the first copy.
bool prepare_tracked_frame(
    const protocol_link_t *link,
    uint8_t message_type,
    uint32_t session_id,
    uint16_t sequence,
    const uint8_t *payload,
    uint16_t payload_length,
    pending_tx_t *pending);
bool send_tracked_frame(
    const protocol_link_t *link,
    uint8_t message_type,
    uint32_t session_id,
    uint16_t sequence,
    const uint8_t *payload,
    uint16_t payload_length,
    pending_tx_t *pending);
bool retry_pending(
    const protocol_link_t *link,
    pending_tx_t *pending);
bool send_ack(
    const protocol_link_t *link,
    uint32_t session_id,
    uint16_t tx_sequence,
    uint16_t acknowledged_sequence,
    uint8_t *scratch,
    size_t scratch_size);
bool send_nack(
    const protocol_link_t *link,
    uint32_t session_id,
    uint16_t tx_sequence,
    uint16_t rejected_sequence,
    uint8_t reason,
    uint8_t *scratch,
    size_t scratch_size);
bool pending_timed_out(const protocol_link_t *link, const pending_tx_t *pending);
#endif
