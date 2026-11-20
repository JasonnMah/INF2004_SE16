#ifndef PICO2_PICO2_PROTOCOL_H
#define PICO2_PICO2_PROTOCOL_H

#include "protocol/reliability.h"
#include "protocol/frame.h"

// Sender gives up after (1 initial attempt + MAX_RETRIES) ACK intervals.
// One extra interval allows its final retry to arrive before we abandon RX.
#define RECEIVE_PROGRESS_TIMEOUT_MS ((MAX_RETRIES + 2u) * ACK_TIMEOUT_MS)

typedef struct pico2_ctx pico2_ctx_t;

// Optional application hooks. The protocol itself does not know source bytes.
// validate_log runs before END is ACKed; NULL accepts matching block counts.
// transfer_complete runs once per validated retrieval, after the ACK, and may
// start an application command. Retransmitted END frames only repeat the ACK.
typedef struct
{
    bool (*drop_data_ack)(uint16_t block);
    bool (*validate_log)(const uint8_t *data, size_t length);
    void (*transfer_complete)(pico2_ctx_t *p2);
    bool (*defer_data_ack)(pico2_ctx_t *p2); /* Batch boundary only; NULL is unchanged. */
} pico2_hooks_t;

typedef enum
{
    P2_WAIT_HELLO_ACK,
    P2_WAIT_CONFIG_ACK,
    P2_WAIT_REQUEST_ACK,
    P2_RECEIVING_DATA,
    P2_WAIT_CLEAR_ACK,
    P2_COMPLETE,
    P2_FAILED,
    // Opt-in application command: acknowledge CONFIG without starting retrieval.
    P2_WAIT_CONFIG_ONLY_ACK

} pico2_state_t;

struct pico2_ctx
{
    protocol_link_t link;
    pico2_hooks_t hooks;
    uint32_t config_timestamp;
    uint32_t config_interval;
    uint32_t clear_ack_timeout_ms; /* 0 uses normal 500 ms. Set only for physical erase. */
    pico2_state_t state;
    uint16_t next_tx_sequence;
    pending_tx_t pending_command;

    uint16_t expected_block;
    uint8_t *received_log;
    size_t received_log_capacity;
    size_t received_log_length;
    // P2_COMPLETE also describes CONFIG/CLEAR success. Only this flag means
    // the buffer contains a completed log suitable for a future upload stage.
    bool received_log_valid;
    uint64_t last_receive_progress_ms;
    uint16_t last_data_sequence;
    size_t last_data_length;
    bool data_ack_deferred;

    // Remember the last completed END independently of application commands.
    bool completed_end_valid;
    uint16_t completed_end_sequence;
    uint16_t completed_end_blocks;

};

// The caller owns receive_buffer for the lifetime of the context. Its capacity
// is the total log limit, independent of MAX_PAYLOAD_SIZE for an individual frame.
bool pico2_init(pico2_ctx_t *p2, protocol_link_t link,
                uint8_t *receive_buffer, size_t receive_capacity,
                uint32_t timestamp, uint32_t interval, pico2_hooks_t hooks);
bool pico2_send_command(
    pico2_ctx_t *p2,
    uint8_t message_type,
    const uint8_t *payload,
    uint16_t payload_length,
    pico2_state_t waiting_state);
// Uses the initialized timestamp/interval and the existing tracked-command path.
// Success reaches P2_COMPLETE; retry exhaustion reaches P2_FAILED.
// Existing demo commands continue to use P2_WAIT_CONFIG_ACK and auto-retrieval.
bool pico2_send_config(pico2_ctx_t *p2);
// Start at block zero with empty receive storage; completion uses existing hooks.
bool pico2_request_data(pico2_ctx_t *p2);
// Explicit application request; uses the existing CLEAR_LOG ACK/retry path.
bool pico2_clear_log(pico2_ctx_t *p2);
// Local cancellation/failure: discard partial-log metadata and stop retries.
// Does not send CLEAR_LOG or modify the Sensor's source buffer.
void pico2_abort(pico2_ctx_t *p2);
void pico2_handle_frame(
    pico2_ctx_t *p2,
    const protocol_frame_t *frame);
void pico2_tick(pico2_ctx_t *p2);
/* Release a deliberately held DATA ACK after the application commits delivery.
 * Duplicates cannot release it, and the receive-idle timer is paused while held. */
bool pico2_confirm_data(pico2_ctx_t *p2);
#endif
