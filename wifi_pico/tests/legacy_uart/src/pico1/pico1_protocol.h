#ifndef PICO1_PICO1_PROTOCOL_H
#define PICO1_PICO1_PROTOCOL_H

#include "protocol/reliability.h"
#include "protocol/frame.h"

typedef struct
{
    bool (*send_data)(const protocol_link_t *link, const pending_tx_t *pending,
                      uint16_t block, const uint8_t *payload, uint16_t length);
    bool (*drop_config_ack)(void);
    // Optional local application notification; sends no extra protocol frame.
    void (*transfer_failed)(void);
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
    pending_tx_t pending_transfer;

    const uint8_t *logger_data;
    size_t logger_data_length;

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
#endif
