/* Range control surrounds the existing Sensor endpoint. It does not rebuild
 * DATA frames or duplicate stop-and-wait/CRC/retransmission implementation. */
#include "batch_sensor.h"
#include <string.h>

static bool configure(void *context, uint32_t stamp, uint32_t interval)
{
    batch_sensor_t *s = context;
    return !s->active && (!s->storage->configure ||
        s->storage->configure(s->storage->context, stamp, interval));
}
static bool snapshot(void *context, const uint8_t **bytes, size_t *length)
{
    batch_sensor_t *s = context;
    if (!s->active || !s->prepared) return false;
    *bytes = s->bytes; *length = s->info.length;
    return true;
}
static pico1_storage_result_t clear_begin(void *context)
{
    batch_sensor_t *s = context;
    return s->active || !s->storage->clear_begin ? P1_STORAGE_ERROR :
        s->storage->clear_begin(s->storage->context);
}
static pico1_storage_result_t clear_poll(void *context)
{
    batch_sensor_t *s = context;
    return s->storage->clear_poll ? s->storage->clear_poll(s->storage->context) : P1_STORAGE_ERROR;
}
static void reply(batch_sensor_t *s, const protocol_frame_t *f, bool accepted)
{
    uint8_t scratch[TX_BUFFER_SIZE];
    if (accepted) send_ack(&s->control, s->control.session_id, s->control_sequence++,
                           f->sequence, scratch, sizeof(scratch));
    else send_nack(&s->control, s->control.session_id, s->control_sequence++, f->sequence,
                   NACK_BAD_PAYLOAD, scratch, sizeof(scratch));
}
static void information(batch_sensor_t *s, uint16_t request_sequence)
{
    uint8_t payload[UART_BATCH_INFO_SIZE], wire[TX_BUFFER_SIZE];
    batch_pack(&s->info, payload, true);
    size_t length = frame_encode(MSG_BATCH_INFO, 0, s->control.session_id,
                                request_sequence, payload, sizeof(payload), wire, sizeof(wire));
    if (length) protocol_write(&s->control, wire, length);
}
bool batch_sensor_init(batch_sensor_t *s, protocol_link_t link,
                        const pico1_backend_t *storage, batch_source_t source)
{
    if (!s || !storage || !source.open || !source.finish || !protocol_link_valid(&link)) return false;
    *s = (batch_sensor_t){.control = link, .storage = storage, .source = source, .control_sequence = 1};
    s->backend = (pico1_backend_t){.context = s, .configure = configure, .snapshot = snapshot,
                                  .clear_begin = clear_begin, .clear_poll = clear_poll};
    return pico1_init(&s->protocol, link, NULL, 0, (pico1_hooks_t){.backend = &s->backend});
}
void batch_sensor_handle(batch_sensor_t *s, const protocol_frame_t *f)
{
    if (f->session_id == s->control.session_id && f->message_type == MSG_BATCH_OPEN) {
        if (f->payload_length != UART_BATCH_OPEN_SIZE || s->protocol.clear_pending) {
            reply(s, f, false); return;
        }
        /* A restarted gateway can reuse a control sequence. Only an exact
         * request (including its new data-session nonce) is a retransmission. */
        if (s->open_cached && f->sequence == s->last_open_sequence &&
            !memcmp(f->payload, s->last_open, sizeof(s->last_open))) {
            information(s, f->sequence); return;
        }
        batch_info_t requested, result;
        batch_unpack(&requested, f->payload, false);
        bool fresh = batch_digest_zero(requested.sha256);
        if (!requested.session || requested.session == s->control.session_id ||
            requested.total > UART_BATCH_MAX_LOG || requested.offset > requested.total ||
            requested.offset % UART_BATCH_BYTES || (fresh && (requested.total || requested.offset)) ||
            !s->source.open(s->source.context, &requested, &result, s->bytes) ||
            !batch_info_valid(&result) || result.session != requested.session ||
            result.offset != requested.offset || (!fresh &&
            (result.total != requested.total || memcmp(result.sha256, requested.sha256, 32)))) {
            reply(s, f, false); return;
        }
        pico1_abort_transfer(&s->protocol);
        protocol_link_t data_link = s->control;
        data_link.session_id = result.session;
        pico1_init(&s->protocol, data_link, NULL, 0, (pico1_hooks_t){.backend = &s->backend});
        s->protocol.last_data_ack_timeout_ms = UART_BATCH_UPLOAD_ACK_MS;
        s->info = result; s->active = s->prepared = s->open_cached = true;
        s->last_open_sequence = f->sequence;
        memcpy(s->last_open, f->payload, sizeof(s->last_open));
        information(s, f->sequence);
        return;
    }
    if (f->session_id == s->control.session_id && f->message_type == MSG_BATCH_DONE) {
        if (f->payload_length != UART_BATCH_DONE_SIZE || s->protocol.clear_pending ||
            (s->active && (read_u32_le(f->payload + 32) != s->info.total ||
                           memcmp(f->payload, s->info.sha256, 32))) ||
            !s->source.finish(s->source.context, f->payload, read_u32_le(f->payload + 32))) {
            reply(s, f, false); return;
        }
        pico1_abort_transfer(&s->protocol);
        s->active = s->prepared = s->open_cached = false;
        s->protocol.link = s->control;
        reply(s, f, true);
        return;
    }
    if (f->session_id == s->control.session_id &&
        (f->message_type == MSG_CONFIG || f->message_type == MSG_CLEAR_LOG)) {
        if (s->active) { reply(s, f, false); return; }
        s->protocol.link = s->control;
        if (f->message_type == MSG_CLEAR_LOG) s->open_cached = false;
    }
    pico1_handle_frame(&s->protocol, f);
}
void batch_sensor_tick(batch_sensor_t *s) { pico1_tick(&s->protocol); }
