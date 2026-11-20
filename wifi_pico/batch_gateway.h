#ifndef WIFI_BATCH_GATEWAY_H
#define WIFI_BATCH_GATEWAY_H
#include "batch_store.h"
#include "batch/batch_protocol.h"
#include "pico2/pico2_protocol.h"
typedef enum {
    BG_IDLE, BG_OPEN, BG_PREPARE, BG_RECEIVE, BG_SAVE, BG_UPLOAD,
    BG_HTTP, BG_COMMIT, BG_END, BG_ADVANCE, BG_DONE, BG_RETRY, BG_FAULT
} batch_gateway_state_t;
enum { BG_EVENT_SAVED, BG_EVENT_CONFIRMED, BG_EVENT_COMPLETE, BG_EVENT_RETRY, BG_EVENT_DUP_END };
typedef struct {
    void *context;
    bool (*upload)(void *, const uint8_t *, unsigned, const char *, unsigned);
    void (*event)(void *, unsigned, uint32_t, uint32_t);
} batch_gateway_io_t;
typedef struct {
    /* First member lets the endpoint callback recover its owning controller. */
    pico2_ctx_t data;
    protocol_link_t control;
    batch_store_t store;
    batch_gateway_io_t io;
    batch_gateway_state_t state;
    batch_info_t info;
    pending_tx_t pending;
    uint8_t bytes[UART_BATCH_BYTES];
    char upload_id[80];
    uint16_t sequence;
    uint32_t nonce;
    uint64_t due;
    bool data_session, end_seen, end_memo_valid;
    uint32_t end_session;
    uint16_t end_sequence, end_blocks;
} batch_gateway_t;
bool batch_gateway_init(batch_gateway_t *, protocol_link_t, batch_flash_t, batch_gateway_io_t);
bool batch_gateway_start(batch_gateway_t *);
void batch_gateway_frame(batch_gateway_t *, const protocol_frame_t *);
void batch_gateway_tick(batch_gateway_t *);
void batch_gateway_upload_result(batch_gateway_t *, int result);
void batch_gateway_rx_fault(batch_gateway_t *);
bool batch_gateway_active(const batch_gateway_t *);
#endif
