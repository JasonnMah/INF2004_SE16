#ifndef PICO1_PICO1_PROTOCOL_H
#define PICO1_PICO1_PROTOCOL_H

#include "protocol/reliability.h"
#include "protocol/frame.h"

/* Flash work is started/polled by the application; ACK means it finished.
 * Snapshot bytes are borrowed until release, including all DATA/END retries. */
typedef enum { P1_STORAGE_READY, P1_STORAGE_PENDING, P1_STORAGE_ERROR } pico1_storage_result_t;
typedef struct {
    void *context;
    bool (*configure)(void *context, uint32_t timestamp, uint32_t interval_seconds);
    bool (*snapshot)(void *context, const uint8_t **bytes, size_t *length);
    pico1_storage_result_t (*clear_begin)(void *context);
    pico1_storage_result_t (*clear_poll)(void *context);
    void (*release)(void *context, bool completed);
} pico1_backend_t;

typedef struct
{
    bool (*send_data)(const protocol_link_t *link, const pending_tx_t *pending,
                      uint16_t block, const uint8_t *payload, uint16_t length);
    bool (*drop_config_ack)(void);
    // Optional local application notification; sends no extra protocol frame.
    void (*transfer_failed)(void);
    const pico1_backend_t *backend; /* NULL preserves the original RAM demo. */
} pico1_hooks_t;

typedef enum
{
    P1_TRANSFER_IDLE,
    P1_WAIT_DATA_ACK,
    P1_WAIT_END_ACK

} pico1_transfer_state_t;

typedef struct
{
    protocol_link_t link;
    pico1_hooks_t hooks;
    bool configured;
    uint32_t current_timestamp;
    uint32_t sampling_interval;

    uint16_t next_tx_sequence;

    bool last_command_valid;
    uint16_t last_command_sequence;
    uint8_t last_command_type;

    pico1_transfer_state_t transfer_state;
    uint16_t current_block;
    uint16_t total_blocks;
    uint32_t last_data_ack_timeout_ms; /* Opt-in batch boundary; zero retains 500 ms. */
    pending_tx_t pending_transfer;

    const uint8_t *logger_data;
    size_t logger_data_length;
    bool source_borrowed;
    bool clear_pending;
    protocol_frame_t clear_command;

} pico1_ctx_t;

// Borrows immutable compressed bytes; caller keeps them alive through transfer.
// CLEAR_LOG clears this context's logical length, not the caller's storage.
// The unchanged 16-bit block count limits length to UINT16_MAX * DATA_CHUNK_SIZE.
bool pico1_init(pico1_ctx_t *p1, protocol_link_t link,
                const uint8_t *compressed_data, size_t compressed_length,
                pico1_hooks_t hooks);
void pico1_handle_frame(
    pico1_ctx_t *p1,
    const protocol_frame_t *frame);
void pico1_tick(pico1_ctx_t *p1);
/* Cancel only DATA/END ownership; an accepted flash clear must still finish. */
void pico1_abort_transfer(pico1_ctx_t *p1);
#endif
