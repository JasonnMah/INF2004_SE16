/* Communication-owner controller. It borrows one immutable RAM body to HTTP,
 * journals progress before ACK/advance, and always revalidates Sensor identity
 * when restarting a range. No SDK/kernel calls or dynamic allocation here. */
#include "batch_gateway.h"
#include "protocol/crc16.h"
#include <string.h>

_Static_assert(BATCH_BYTES == UART_BATCH_BYTES, "Batch storage and transport sizes");
static void event(batch_gateway_t *g, unsigned kind)
{
    if (g->io.event) g->io.event(g->io.context, kind, g->store.record.offset,
                               g->store.record.total);
}
static bool hold_ack(pico2_ctx_t *p)
{
    batch_gateway_t *g = (batch_gateway_t *)p;
    return p->received_log_length == g->info.length;
}
static void end_received(pico2_ctx_t *p)
{
    batch_gateway_t *g = (batch_gateway_t *)p;
    g->end_seen = g->end_memo_valid = true;
    g->end_session = p->link.session_id;
    g->end_sequence = p->completed_end_sequence;
    g->end_blocks = p->completed_end_blocks;
}
static void identity_from_record(batch_gateway_t *g)
{
    batch_record_t *r = &g->store.record;
    memcpy(g->info.sha256, r->log_sha256, 32);
    g->info.total = r->total; g->info.offset = r->offset;
    g->info.length = (uint16_t)r->length; g->info.crc = r->data_crc;
}
static void make_upload_id(batch_gateway_t *g)
{
    static const char hex[] = "0123456789abcdef";
    memcpy(g->upload_id, "batch-", 6);
    for (unsigned i = 0; i < 32; ++i) {
        g->upload_id[6 + 2*i] = hex[g->info.sha256[i] >> 4];
        g->upload_id[7 + 2*i] = hex[g->info.sha256[i] & 15];
    }
    g->upload_id[70] = '-';
    for (unsigned i = 0; i < 8; ++i)
        g->upload_id[71 + i] = hex[(g->info.offset >> (28 - 4*i)) & 15];
    g->upload_id[79] = 0;
}
static void recover(batch_gateway_t *g)
{
    g->pending.active = false;
    if (g->data_session) pico2_abort(&g->data);
    g->data_session = false;
    batch_flash_t flash = g->store.flash;
    if (!batch_store_open(&g->store, flash)) { g->state = BG_FAULT; return; }
    identity_from_record(g);
    switch (g->store.record.state) {
    case BATCH_IDLE: g->state = BG_IDLE; break;
    case BATCH_FETCHING: g->state = BG_OPEN; break;
    case BATCH_READY:
        if (g->info.length) memcpy(g->bytes, batch_store_data(&g->store), g->info.length);
        make_upload_id(g); g->state = BG_UPLOAD; break;
    case BATCH_CONFIRMED: g->state = BG_COMMIT; break;
    }
}
static void retry(batch_gateway_t *g)
{
    g->pending.active = false;
    if (g->data_session) pico2_abort(&g->data);
    g->data_session = false;
    g->state = BG_RETRY;
    g->due = protocol_now_ms(&g->control) + 5000;
    event(g, BG_EVENT_RETRY);
}
bool batch_gateway_init(batch_gateway_t *g, protocol_link_t link, batch_flash_t flash,
                        batch_gateway_io_t io)
{
    if (!g || !protocol_link_valid(&link) || !io.upload) return false;
    *g = (batch_gateway_t){.control = link, .io = io, .sequence = 1,
        .nonce = (uint32_t)protocol_now_ms(&link) ^ 0x42540001u};
    if (!batch_store_open(&g->store, flash)) { g->state = BG_FAULT; return false; }
    g->nonce ^= g->store.record.generation * 0x9e3779b9u;
    recover(g);
    return g->state != BG_FAULT;
}
bool batch_gateway_start(batch_gateway_t *g)
{
    if (g->state != BG_IDLE) return false;
    uint8_t fresh[32] = {0};
    /* Persist intent before OPEN; even a reset before INFO retries the request. */
    if (!batch_store_begin(&g->store, fresh, 0, 0)) { retry(g); return false; }
    recover(g);
    return true;
}
bool batch_gateway_active(const batch_gateway_t *g) { return g->state != BG_IDLE; }
void batch_gateway_upload_result(batch_gateway_t *g, int result)
{
    if (g->state != BG_HTTP || !result) return;
    if (result > 0) g->state = BG_COMMIT;
    else {
        g->state = BG_UPLOAD;
        g->due = protocol_now_ms(&g->control) + 5000;
        event(g, BG_EVENT_RETRY);
    }
}
void batch_gateway_rx_fault(batch_gateway_t *g)
{
    /* While HTTP borrows bytes, no reset/re-fetch may modify that body. */
    if (g->state != BG_HTTP && g->state != BG_UPLOAD && g->state != BG_COMMIT &&
        g->state != BG_IDLE && g->state != BG_FAULT) retry(g);
}
void batch_gateway_frame(batch_gateway_t *g, const protocol_frame_t *f)
{
    if (g->end_memo_valid && f->session_id == g->end_session &&
        f->message_type == MSG_TRANSFER_END && f->sequence == g->end_sequence &&
        f->payload_length == 2 && read_u16_le(f->payload) == g->end_blocks) {
        uint8_t scratch[TX_BUFFER_SIZE];
        send_ack(&g->control, f->session_id, g->sequence++, f->sequence, scratch, sizeof(scratch));
        event(g, BG_EVENT_DUP_END); return;
    }
    if (g->pending.active && f->session_id == g->control.session_id) {
        if (f->message_type == MSG_NACK && f->payload_length == 3 &&
            read_u16_le(f->payload) == g->pending.sequence) { retry(g); return; }
        if (g->state == BG_DONE && f->message_type == MSG_ACK && f->payload_length == 2 &&
            read_u16_le(f->payload) == g->pending.sequence) {
            g->pending.active = false;
            if (batch_store_finish(&g->store)) {
                g->state = BG_IDLE; g->data_session = false;
                event(g, BG_EVENT_COMPLETE);
            } else retry(g);
            return;
        }
        if (g->state == BG_OPEN && f->message_type == MSG_BATCH_INFO &&
            f->sequence == g->pending.sequence && f->payload_length == UART_BATCH_INFO_SIZE) {
            batch_info_t info;
            batch_unpack(&info, f->payload, true);
            if (!batch_info_valid(&info) || info.session != g->info.session ||
                info.offset != g->info.offset || batch_digest_zero(info.sha256) ||
                (!batch_digest_zero(g->info.sha256) &&
                 (info.total != g->info.total || memcmp(info.sha256, g->info.sha256, 32)))) return;
            g->pending.active = false; g->info = info; g->state = BG_PREPARE;
            return;
        }
    }
    if (g->data_session && f->session_id == g->data.link.session_id)
        pico2_handle_frame(&g->data, f);
}
void batch_gateway_tick(batch_gateway_t *g)
{
    uint64_t now = protocol_now_ms(&g->control);
    if (g->state == BG_IDLE || g->state == BG_FAULT) return;
    if (g->state == BG_RETRY) {
        if (now >= g->due) recover(g);
        return;
    }
    if (g->pending.active) {
        if (pending_timed_out(&g->control, &g->pending) &&
            !retry_pending(&g->control, &g->pending)) retry(g);
        return;
    }
    switch (g->state) {
    case BG_OPEN: {
        /* Session changes isolate stale DATA from a previous range or reset. */
        do { ++g->nonce; } while (!g->nonce || g->nonce == g->control.session_id);
        g->info.session = g->nonce;
        uint8_t payload[UART_BATCH_OPEN_SIZE];
        batch_pack(&g->info, payload, false);
        if (!send_tracked_frame(&g->control, MSG_BATCH_OPEN, g->control.session_id,
                g->sequence++, payload, sizeof(payload), &g->pending)) { retry(g); break; }
        g->pending.timeout_ms = UART_BATCH_CONTROL_MS;
        break;
    }
    case BG_PREPARE: {
        if (!batch_store_begin(&g->store, g->info.sha256, g->info.total, g->info.offset)) {
            retry(g); break;
        }
        protocol_link_t link = g->control; link.session_id = g->info.session;
        if (!pico2_init(&g->data, link, g->bytes, sizeof(g->bytes), 0, 60,
            (pico2_hooks_t){.defer_data_ack = hold_ack, .transfer_complete = end_received})) {
            retry(g); break;
        }
        g->data.state = P2_COMPLETE; g->data_session = true; g->end_seen = false;
        if (!pico2_request_data(&g->data)) { retry(g); break; }
        g->state = BG_RECEIVE;
        break;
    }
    case BG_RECEIVE:
        pico2_tick(&g->data);
        if (g->data.state == P2_FAILED) retry(g);
        else if (g->data.data_ack_deferred || g->data.received_log_valid) {
            if (g->data.received_log_length != g->info.length ||
                crc16_x25(g->bytes, g->info.length) != g->info.crc) retry(g);
            else g->state = BG_SAVE;
        }
        break;
    case BG_SAVE:
        if (!batch_store_save(&g->store, g->bytes, g->info.length)) { retry(g); break; }
        make_upload_id(g); g->due = 0; g->state = BG_UPLOAD;
        event(g, BG_EVENT_SAVED);
        break;
    case BG_UPLOAD:
        if (now >= g->due && g->io.upload(g->io.context, g->bytes, g->info.length,
                                          g->upload_id, g->info.total)) g->state = BG_HTTP;
        break;
    case BG_COMMIT:
        if (g->store.record.state == BATCH_READY) {
            if (!batch_store_confirm(&g->store)) { retry(g); break; }
        } else if (!batch_store_release_confirmed(&g->store)) { retry(g); break; }
        event(g, BG_EVENT_CONFIRMED);
        if (g->data_session && g->data.data_ack_deferred && pico2_confirm_data(&g->data)) {
            g->state = BG_END; g->due = now + RECEIVE_PROGRESS_TIMEOUT_MS;
        } else g->state = BG_ADVANCE; /* Restart recovery reopens the next range. */
        break;
    case BG_END:
        if (g->end_seen || now >= g->due) g->state = BG_ADVANCE;
        break;
    case BG_ADVANCE: {
        g->data_session = false;
        identity_from_record(g);
        uint32_t next = g->info.offset + g->info.length;
        if (next < g->info.total) {
            g->info.offset = next; g->state = BG_OPEN;
        } else g->state = BG_DONE;
        break;
    }
    case BG_DONE: {
        uint8_t payload[UART_BATCH_DONE_SIZE];
        memcpy(payload, g->info.sha256, 32); write_u32_le(payload + 32, g->info.total);
        if (!send_tracked_frame(&g->control, MSG_BATCH_DONE, g->control.session_id,
                g->sequence++, payload, sizeof(payload), &g->pending)) { retry(g); break; }
        g->pending.timeout_ms = UART_BATCH_CONTROL_MS;
        break;
    }
    default: break;
    }
}
