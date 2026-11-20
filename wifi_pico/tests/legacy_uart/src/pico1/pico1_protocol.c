#include "pico1_protocol.h"
#include <string.h>

bool pico1_init(pico1_ctx_t *p1, protocol_link_t link,
                const uint8_t *compressed_data, size_t compressed_length,
                pico1_hooks_t hooks)
{
    if ((compressed_length > 0 && compressed_data == NULL) ||
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
    send_ack(p1->link.uart, p1->link.session_id, p1->next_tx_sequence++,
             frame->sequence, scratch, sizeof(scratch));
}

static void pico1_nack(pico1_ctx_t *p1, const protocol_frame_t *frame,
                       protocol_nack_reason_t reason)
{
    uint8_t scratch[TX_BUFFER_SIZE];
    send_nack(p1->link.uart, p1->link.session_id, p1->next_tx_sequence++,
              frame->sequence, reason, scratch, sizeof(scratch));
}

static void remember_command(pico1_ctx_t *p1, const protocol_frame_t *frame)
{
    p1->last_command_valid = true;
    p1->last_command_sequence = frame->sequence;
    p1->last_command_type = frame->message_type;
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
        if (!prepare_tracked_frame(MSG_DATA, p1->link.session_id, sequence,
                                   payload, (uint16_t)(DATA_BLOCK_INDEX_SIZE + chunk_length),
                                   &p1->pending_transfer))
        {
            return;
        }

        bool sent_by_hook = p1->hooks.send_data != NULL &&
            p1->hooks.send_data(&p1->link, &p1->pending_transfer,
                                p1->current_block, payload,
                                (uint16_t)(DATA_BLOCK_INDEX_SIZE + chunk_length));
        if (!sent_by_hook)
        {
            uart_write_blocking(p1->link.uart, p1->pending_transfer.frame,
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
            p1->link.uart,
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
        if (retry_pending(p1->link.uart, &p1->pending_transfer))
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

    bool duplicate_command = p1->last_command_valid &&
        frame->sequence == p1->last_command_sequence &&
        frame->message_type == p1->last_command_type;
    if (duplicate_command)
    {
        pico1_ack(p1, frame);
        return;
    }

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
    if (!pending_timed_out(&p1->pending_transfer))
    {
        return;
    }

    if (!retry_pending(p1->link.uart, &p1->pending_transfer))
    {
        protocol_log(&p1->link, "\n[P1] Transfer timeout: retry limit reached\n");
        p1->pending_transfer.active = false;
        p1->transfer_state = P1_TRANSFER_IDLE;
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
