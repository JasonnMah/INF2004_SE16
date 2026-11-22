#ifndef UART_BATCH_SENSOR_H
#define UART_BATCH_SENSOR_H
#include "batch_protocol.h"
#include "pico1/pico1_protocol.h"
typedef struct {
    void *context;
    /* Fresh request: zero digest/total/offset. Resume: verify the exact frozen
     * prefix, then copy one range into the provided 4 KiB buffer. */
    bool (*open)(void *, const batch_info_t *, batch_info_t *, uint8_t *);
    /* Verify identity and release the frozen prefix; do not erase the log.
     * Must accept the same completed identity again if the DONE ACK was lost. */
    bool (*finish)(void *, const uint8_t sha256[32], uint32_t total);
} batch_source_t;
typedef struct {
    pico1_ctx_t protocol;
    protocol_link_t control;
    const pico1_backend_t *storage;
    pico1_backend_t backend;
    batch_source_t source;
    batch_info_t info;
    uint8_t bytes[UART_BATCH_BYTES], last_open[UART_BATCH_OPEN_SIZE];
    uint16_t last_open_sequence, control_sequence;
    bool active, prepared, open_cached;
} batch_sensor_t;
/* All objects/callback contexts must outlive the endpoint. Call handle for
 * decoded frames and tick regularly from one owner loop, never from the ISR.
 * CONFIG/CLEAR reuse the existing backend; its old snapshot/release hooks are
 * replaced by the frozen-range callbacks above for this opt-in endpoint. */
bool batch_sensor_init(batch_sensor_t *, protocol_link_t, const pico1_backend_t *, batch_source_t);
void batch_sensor_handle(batch_sensor_t *, const protocol_frame_t *);
void batch_sensor_tick(batch_sensor_t *);
#endif
