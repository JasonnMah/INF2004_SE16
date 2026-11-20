#include "pico1_protocol.h"
#include <string.h>

bool pico1_init(pico1_ctx_t *p1, protocol_link_t link,
                const uint8_t *compressed_data, size_t compressed_length,
                pico1_hooks_t hooks)
{
    if (!protocol_link_valid(&link) || (compressed_length > 0 && compressed_data == NULL) ||
        compressed_length > (size_t)UINT16_MAX * DATA_CHUNK_SIZE)
    {
        return false;
    }
    *p1 = (pico1_ctx_t){0};
    p1->link = link;
    p1->hooks = hooks;
    p1->logger_data = compressed_data;
    p1->logger_data_length = compressed_length;
    p1->next_tx_sequence = 1;
    p1->transfer_state = P1_TRANSFER_IDLE;
    return true;
}

static void pico1_ack(pico1_ctx_t *p1, const protocol_frame_t *frame)
{
    uint8_t scratch[TX_BUFFER_SIZE];
    send_ack(&p1->link, p1->link.session_id, p1->next_tx_sequence++,
             frame->sequence, scratch, sizeof(scratch));
}

static void pico1_nack(pico1_ctx_t *p1, const protocol_frame_t *frame,
                       protocol_nack_reason_t reason)
{
    uint8_t scratch[TX_BUFFER_SIZE];
    send_nack(&p1->link, p1->link.session_id, p1->next_tx_sequence++,
              frame->sequence, reason, scratch, sizeof(scratch));
}

static void remember_command(pico1_ctx_t *p1, const protocol_frame_t *frame)
{
    p1->last_command_valid = true;
    p1->last_command_sequence = frame->sequence;
    p1->last_command_type = frame->message_type;
}

static void release_source(pico1_ctx_t *p1, bool completed)
{
    const pico1_backend_t *backend = p1->hooks.backend;
    if (p1->source_borrowed && backend != NULL && backend->release != NULL) {
        backend->release(backend->context, completed);
    }
    p1->source_borrowed = false;
}

static void pico1_send_next_transfer_frame(pico1_ctx_t *p1)
{
    if (p1->current_block < p1->total_blocks)
    {
        size_t offset =
            (size_t)p1->current_block * DATA_CHUNK_SIZE;

        size_t remaining =
            p1->logger_data_length - offset;

        size_t chunk_length =
            remaining > DATA_CHUNK_SIZE ?
            DATA_CHUNK_SIZE : remaining;

        uint8_t payload[DATA_BLOCK_INDEX_SIZE + DATA_CHUNK_SIZE];

        write_u16_le(payload, p1->current_block);

        memcpy(&payload[DATA_BLOCK_INDEX_SIZE], &p1->logger_data[offset], chunk_length);

        uint16_t sequence = p1->next_tx_sequence++;

        // Keep the correct copy for timeout/NACK recovery. A demo hook may
        // replace only the initial transmission.
        if (!prepare_tracked_frame(&p1->link, MSG_DATA, p1->link.session_id, sequence,
                                   payload, (uint16_t)(DATA_BLOCK_INDEX_SIZE + chunk_length),
                                   &p1->pending_transfer))
        {
            return;
        }

        if (p1->last_data_ack_timeout_ms && p1->current_block + 1u == p1->total_blocks)
            p1->pending_transfer.timeout_ms = p1->last_data_ack_timeout_ms;
        bool sent_by_hook = p1->hooks.send_data != NULL &&
            p1->hooks.send_data(&p1->link, &p1->pending_transfer,
                                p1->current_block, payload,
                                (uint16_t)(DATA_BLOCK_INDEX_SIZE + chunk_length));
        if (!sent_by_hook)
        {
            protocol_write(&p1->link, p1->pending_transfer.frame,
                                p1->pending_transfer.frame_length);
        }

        p1->transfer_state = P1_WAIT_DATA_ACK;
    }
    else
    {
        uint8_t payload[TRANSFER_END_PAYLOAD_SIZE];
        write_u16_le(payload, p1->total_blocks);

        uint16_t sequence = p1->next_tx_sequence++;

        bool ok = send_tracked_frame(
            &p1->link,
            MSG_TRANSFER_END,
            p1->link.session_id,
            sequence,
            payload,
            sizeof(payload),
            &p1->pending_transfer
        );

        if (ok)
        {
            p1->transfer_state = P1_WAIT_END_ACK;
        }
    }
}

static void pico1_handle_ack(pico1_ctx_t *p1,
                             const protocol_frame_t *frame)
{
    if (frame->payload_length != ACK_PAYLOAD_SIZE)
    {
        return;
    }

    uint16_t acked_sequence = read_u16_le(frame->payload);

    if (!p1->pending_transfer.active ||
        acked_sequence != p1->pending_transfer.sequence)
    {
        return;
    }

    p1->pending_transfer.active = false;

    if (p1->transfer_state == P1_WAIT_DATA_ACK)
    {
        p1->current_block++;
        pico1_send_next_transfer_frame(p1);
    }
    else if (p1->transfer_state == P1_WAIT_END_ACK)
    {
        p1->transfer_state = P1_TRANSFER_IDLE;
        release_source(p1, true);
    }
}

static void pico1_handle_nack(pico1_ctx_t *p1,
                              const protocol_frame_t *frame)
{
    if (frame->payload_length != NACK_PAYLOAD_SIZE)
    {
        return;
    }

    uint16_t rejected_sequence = read_u16_le(frame->payload);

    if (p1->pending_transfer.active &&
        rejected_sequence == p1->pending_transfer.sequence)
    {
        if (retry_pending(&p1->link, &p1->pending_transfer))
        {
            protocol_log(&p1->link, "[P1] NACK received; retransmitted correct transfer frame\n");
        }
    }
}

static void pico1_handle_hello(pico1_ctx_t *p1,
                               const protocol_frame_t *frame)
{
    if (frame->payload_length != 0)
    {
        pico1_nack(p1, frame, NACK_BAD_PAYLOAD);
        return;
    }

    remember_command(p1, frame);

    pico1_ack(p1, frame);
}

static void pico1_handle_config(pico1_ctx_t *p1,
                                const protocol_frame_t *frame)
{
    if (frame->payload_length != CONFIG_PAYLOAD_SIZE)
    {
        pico1_nack(p1, frame, NACK_BAD_PAYLOAD);
        return;
    }

    p1->current_timestamp = read_u32_le(&frame->payload[0]);
    p1->sampling_interval = read_u32_le(&frame->payload[CONFIG_INTERVAL_OFFSET]);
    p1->configured = true;

    remember_command(p1, frame);

    if (p1->hooks.drop_config_ack != NULL && p1->hooks.drop_config_ack())
    {
        return;
    }

    pico1_ack(p1, frame);
}

static void pico1_handle_request_data(pico1_ctx_t *p1,
                                      const protocol_frame_t *frame)
{
    if (frame->payload_length != REQUEST_DATA_PAYLOAD_SIZE)
    {
        pico1_nack(p1, frame, NACK_BAD_PAYLOAD);
        return;
    }

    uint16_t start_block = read_u16_le(frame->payload);

    p1->total_blocks =
        (uint16_t)((p1->logger_data_length + DATA_CHUNK_SIZE - 1) /
                   DATA_CHUNK_SIZE);

    if (start_block > p1->total_blocks)
    {
        pico1_nack(p1, frame, NACK_BAD_BLOCK);
        return;
    }

    remember_command(p1, frame);

    pico1_ack(p1, frame);

    p1->current_block = start_block;
    p1->transfer_state = P1_TRANSFER_IDLE;
    p1->pending_transfer.active = false;

    pico1_send_next_transfer_frame(p1);
}

static void pico1_handle_clear_log(pico1_ctx_t *p1,
                                   const protocol_frame_t *frame)
{
    if (frame->payload_length != 0)
    {
        pico1_nack(p1, frame, NACK_BAD_PAYLOAD);
        return;
    }

    p1->logger_data_length = 0;
    p1->total_blocks = 0;
    p1->current_block = 0;
    p1->transfer_state = P1_TRANSFER_IDLE;
    p1->pending_transfer.active = false;

    remember_command(p1, frame);

    pico1_ack(p1, frame);
}

/* Called only for a fresh command; duplicate commands never repeat storage work. */
static bool prepare_backend(pico1_ctx_t *p1, const protocol_frame_t *frame)
{
    const pico1_backend_t *backend = p1->hooks.backend;
    if (backend == NULL) { return true; }
    if ((frame->message_type == MSG_CONFIG || frame->message_type == MSG_CLEAR_LOG) &&
        p1->transfer_state != P1_TRANSFER_IDLE) {
        pico1_nack(p1, frame, NACK_UNEXPECTED_MSG);
        return false;
    }
    if (frame->message_type == MSG_CONFIG && frame->payload_length == CONFIG_PAYLOAD_SIZE &&
        backend->configure != NULL &&
        !backend->configure(backend->context, read_u32_le(frame->payload),
                            read_u32_le(frame->payload + CONFIG_INTERVAL_OFFSET))) {
        pico1_nack(p1, frame, NACK_BAD_PAYLOAD);
        return false;
    }
    if (frame->message_type == MSG_REQUEST_DATA && frame->payload_length == REQUEST_DATA_PAYLOAD_SIZE &&
        backend->snapshot != NULL) {
        /* A fresh request replaces an interrupted transfer, never mutates its source. */
        release_source(p1, false);
        p1->pending_transfer.active = false;
        p1->transfer_state = P1_TRANSFER_IDLE;
        const uint8_t *bytes = NULL;
        size_t length = 0;
        if (!backend->snapshot(backend->context, &bytes, &length)) {
            pico1_nack(p1, frame, NACK_BAD_PAYLOAD);
            return false;
        }
        p1->source_borrowed = true;
        if ((length != 0 && bytes == NULL) || length > (size_t)UINT16_MAX * DATA_CHUNK_SIZE ||
            read_u16_le(frame->payload) > (length + DATA_CHUNK_SIZE - 1) / DATA_CHUNK_SIZE) {
            release_source(p1, false);
            pico1_nack(p1, frame, NACK_BAD_PAYLOAD);
            return false;
        }
        p1->logger_data = bytes;
        p1->logger_data_length = length;
    }
    if (frame->message_type == MSG_CLEAR_LOG && frame->payload_length == 0 && backend->clear_begin != NULL) {
        pico1_storage_result_t result = backend->clear_begin(backend->context);
        if (result == P1_STORAGE_PENDING) {
            p1->clear_pending = true;
            p1->clear_command = *frame;
            return false;
        }
        if (result == P1_STORAGE_ERROR) {
            pico1_nack(p1, frame, NACK_BAD_PAYLOAD);
            return false;
        }
    }
    return true;
}

void pico1_handle_frame(pico1_ctx_t *p1, const protocol_frame_t *frame)
{
    if (frame->session_id != p1->link.session_id)
    {
        return;
    }
    // Transfer responses must bypass the command-duplicate check.
    if (frame->message_type == MSG_ACK)
    {
        pico1_handle_ack(p1, frame);
        return;
    }
    if (frame->message_type == MSG_NACK)
    {
        pico1_handle_nack(p1, frame);
        return;
    }

    if (p1->clear_pending) {
        /* Silence means the same accepted flash operation is still running.
         * Its eventual ACK is sent once all erase steps and verification pass. */
        if (frame->message_type != p1->clear_command.message_type ||
            frame->sequence != p1->clear_command.sequence) {
            pico1_nack(p1, frame, NACK_UNEXPECTED_MSG);
        }
        return;
    }
    bool duplicate_command = p1->last_command_valid &&
        frame->sequence == p1->last_command_sequence &&
        frame->message_type == p1->last_command_type;
    if (duplicate_command)
    {
        pico1_ack(p1, frame);
        return;
    }

    if (!prepare_backend(p1, frame)) { return; }

    switch (frame->message_type)
    {
        case MSG_HELLO:        pico1_handle_hello(p1, frame); break;
        case MSG_CONFIG:       pico1_handle_config(p1, frame); break;
        case MSG_REQUEST_DATA: pico1_handle_request_data(p1, frame); break;
        case MSG_CLEAR_LOG:    pico1_handle_clear_log(p1, frame); break;
        default:              pico1_nack(p1, frame, NACK_UNEXPECTED_MSG); break;
    }
}

void pico1_tick(pico1_ctx_t *p1)
{
    if (p1->clear_pending) {
        const pico1_backend_t *backend = p1->hooks.backend;
        pico1_storage_result_t result = backend->clear_poll != NULL ?
            backend->clear_poll(backend->context) : P1_STORAGE_ERROR;
        if (result == P1_STORAGE_READY) {
            p1->clear_pending = false;
            pico1_handle_clear_log(p1, &p1->clear_command);
        } else if (result == P1_STORAGE_ERROR) {
            p1->clear_pending = false;
            pico1_nack(p1, &p1->clear_command, NACK_BAD_PAYLOAD);
        }
        return;
    }
    if (!pending_timed_out(&p1->link, &p1->pending_transfer))
    {
        return;
    }

    if (!retry_pending(&p1->link, &p1->pending_transfer))
    {
        protocol_log(&p1->link, "\n[P1] Transfer timeout: retry limit reached\n");
        p1->pending_transfer.active = false;
        p1->transfer_state = P1_TRANSFER_IDLE;
        release_source(p1, false);
        if (p1->hooks.transfer_failed != NULL)
        {
            p1->hooks.transfer_failed();
        }
    }
    else
    {
        protocol_log(&p1->link, "\n[P1] Transfer timeout: retransmitted pending frame\n");
    }
}

void pico1_abort_transfer(pico1_ctx_t *p1)
{
    p1->pending_transfer.active = false;
    p1->transfer_state = P1_TRANSFER_IDLE;
    release_source(p1, false);
}
