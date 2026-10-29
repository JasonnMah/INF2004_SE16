#include "pico2_protocol.h"
#include <string.h>

static void reset_received_log(pico2_ctx_t *p2)
{
    p2->expected_block = 0;
    p2->received_log_length = 0;
    p2->received_log_valid = false;
    p2->last_data_length = 0;
}

void pico2_abort(pico2_ctx_t *p2)
{
    p2->pending_command.active = false;
    reset_received_log(p2);
    p2->state = P2_FAILED;
}

static void check_receive_timeout(pico2_ctx_t *p2)
{
    if (p2->state == P2_RECEIVING_DATA &&
        protocol_now_ms(&p2->link) - p2->last_receive_progress_ms >= RECEIVE_PROGRESS_TIMEOUT_MS)
    {
        protocol_log(&p2->link, "[P2] Receive timeout: no transfer progress\n");
        pico2_abort(p2);
    }
}

bool pico2_init(pico2_ctx_t *p2, protocol_link_t link,
                uint8_t *receive_buffer, size_t receive_capacity,
                uint32_t timestamp, uint32_t interval, pico2_hooks_t hooks)
{
    if (!protocol_link_valid(&link) || receive_buffer == NULL || receive_capacity == 0)
    {
        return false;
    }
    *p2 = (pico2_ctx_t){0};
    p2->link = link;
    p2->hooks = hooks;
    p2->received_log = receive_buffer;
    p2->received_log_capacity = receive_capacity;
    p2->config_timestamp = timestamp;
    p2->config_interval = interval;
    p2->next_tx_sequence = 1;
    return true;
}

static void pico2_ack(pico2_ctx_t *p2, const protocol_frame_t *frame)
{
    uint8_t scratch[TX_BUFFER_SIZE];
    send_ack(&p2->link, p2->link.session_id, p2->next_tx_sequence++,
             frame->sequence, scratch, sizeof(scratch));
}

static void pico2_nack(pico2_ctx_t *p2, const protocol_frame_t *frame,
                       protocol_nack_reason_t reason)
{
    uint8_t scratch[TX_BUFFER_SIZE];
    send_nack(&p2->link, p2->link.session_id, p2->next_tx_sequence++,
              frame->sequence, reason, scratch, sizeof(scratch));
}

bool pico2_send_command(
    pico2_ctx_t *p2,
    uint8_t message_type,
    const uint8_t *payload,
    uint16_t payload_length,
    pico2_state_t waiting_state)
{
    if ((p2->pending_command.active && p2->state != P2_FAILED) ||
        p2->state == P2_RECEIVING_DATA)
    {
        return false;
    }
    uint16_t sequence = p2->next_tx_sequence++;

    bool ok = send_tracked_frame(
        &p2->link,
        message_type,
        p2->link.session_id,
        sequence,
        payload,
        payload_length,
        &p2->pending_command
    );

    if (ok)
    {
        if (message_type == MSG_REQUEST_DATA)
        {
            reset_received_log(p2);
        }
        if (message_type == MSG_CLEAR_LOG && p2->clear_ack_timeout_ms != 0) {
            p2->pending_command.timeout_ms = p2->clear_ack_timeout_ms;
        }
        p2->state = waiting_state;
    }

    return ok;
}

bool pico2_send_config(pico2_ctx_t *p2)
{
    // Do not overwrite an outstanding command. A failed command may be replaced
    // by a fresh application request with a new sequence and a fresh retry budget.
    if (p2->pending_command.active && p2->state != P2_FAILED)
    {
        return false;
    }
    uint8_t payload[CONFIG_PAYLOAD_SIZE];
    write_u32_le(payload, p2->config_timestamp);
    write_u32_le(&payload[CONFIG_INTERVAL_OFFSET], p2->config_interval);
    return pico2_send_command(p2, MSG_CONFIG, payload, sizeof(payload),
                               P2_WAIT_CONFIG_ONLY_ACK);
}

bool pico2_request_data(pico2_ctx_t *p2)
{
    if (p2->state != P2_COMPLETE && p2->state != P2_FAILED)
    {
        return false;
    }
    uint8_t payload[REQUEST_DATA_PAYLOAD_SIZE];
    write_u16_le(payload, 0);
    return pico2_send_command(p2, MSG_REQUEST_DATA, payload, sizeof(payload),
                             P2_WAIT_REQUEST_ACK);
}

bool pico2_clear_log(pico2_ctx_t *p2)
{
    if (p2->state != P2_COMPLETE && p2->state != P2_FAILED)
    {
        return false;
    }
    return pico2_send_command(p2, MSG_CLEAR_LOG, NULL, 0, P2_WAIT_CLEAR_ACK);
}

static void pico2_handle_ack(pico2_ctx_t *p2,
                             const protocol_frame_t *frame)
{
    if (frame->payload_length != ACK_PAYLOAD_SIZE ||
        !p2->pending_command.active)
    {
        return;
    }

    uint16_t acked_sequence = read_u16_le(frame->payload);

    if (acked_sequence != p2->pending_command.sequence)
    {
        return;
    }

    p2->pending_command.active = false;

    if (p2->state == P2_WAIT_HELLO_ACK)
    {
        protocol_log(&p2->link, "\n[P2] HELLO acknowledged\n");

        uint8_t config_payload[CONFIG_PAYLOAD_SIZE];
        write_u32_le(&config_payload[0], p2->config_timestamp);
        write_u32_le(&config_payload[CONFIG_INTERVAL_OFFSET], p2->config_interval);

        if (!pico2_send_command(
                p2,
                MSG_CONFIG,
                config_payload,
                sizeof(config_payload),
                P2_WAIT_CONFIG_ACK))
        {
            pico2_abort(p2);
        }
    }
    else if (p2->state == P2_WAIT_CONFIG_ACK)
    {
        protocol_log(&p2->link, "[P2] CONFIG acknowledged\n");

        uint8_t request_payload[REQUEST_DATA_PAYLOAD_SIZE];
        write_u16_le(request_payload, 0);

        if (!pico2_send_command(
                p2,
                MSG_REQUEST_DATA,
                request_payload,
                sizeof(request_payload),
                P2_WAIT_REQUEST_ACK))
        {
            pico2_abort(p2);
        }
    }
    else if (p2->state == P2_WAIT_REQUEST_ACK)
    {
        protocol_log(&p2->link, "[P2] REQUEST_DATA acknowledged\n");
        p2->state = P2_RECEIVING_DATA;
        p2->last_receive_progress_ms = protocol_now_ms(&p2->link);
    }
    else if (p2->state == P2_WAIT_CLEAR_ACK)
    {
        protocol_log(&p2->link, "[P2] CLEAR_LOG acknowledged\n");
        p2->state = P2_COMPLETE;
    }
    else if (p2->state == P2_WAIT_CONFIG_ONLY_ACK)
    {
        protocol_log(&p2->link, "[P2] CONFIG acknowledged\n");
        p2->state = P2_COMPLETE;
    }
}

static void pico2_handle_nack(pico2_ctx_t *p2,
                              const protocol_frame_t *frame)
{
    if (frame->payload_length != NACK_PAYLOAD_SIZE ||
        !p2->pending_command.active)
    {
        return;
    }

    uint16_t rejected_sequence = read_u16_le(frame->payload);

    if (rejected_sequence == p2->pending_command.sequence)
    {
        protocol_log(&p2->link, "\n[P2] NACK received; retrying command\n");

        if (!retry_pending(&p2->link, &p2->pending_command))
        {
            protocol_log(&p2->link, "[P2] Retry limit reached\n");
            pico2_abort(p2);
        }
    }
}

static void pico2_handle_data(pico2_ctx_t *p2,
                              const protocol_frame_t *frame)
{
    // If the REQUEST ACK was lost, let both endpoints' existing timers retry.
    // Do not treat early/stale DATA as proof that a request was acknowledged.
    if (p2->state != P2_RECEIVING_DATA)
    {
        return;
    }
    if (frame->payload_length <= DATA_BLOCK_INDEX_SIZE)
    {
        pico2_nack(p2, frame, NACK_BAD_PAYLOAD);
        return;
    }

    uint16_t block_index = read_u16_le(frame->payload);
    size_t data_length = frame->payload_length - DATA_BLOCK_INDEX_SIZE;

    // New block in expected order.
    if (block_index == p2->expected_block)
    {
        if (data_length > p2->received_log_capacity - p2->received_log_length ||
            p2->expected_block == UINT16_MAX)
        {
            pico2_nack(p2, frame, NACK_BAD_PAYLOAD);
            protocol_log(&p2->link, "[P2] Receive capacity exceeded\n");
            pico2_abort(p2);
            return;
        }

        memcpy(&p2->received_log[p2->received_log_length],
               &frame->payload[DATA_BLOCK_INDEX_SIZE], data_length);
        p2->received_log_length += data_length;

        p2->expected_block++;
        p2->last_data_sequence = frame->sequence;
        p2->last_data_length = data_length;
        p2->last_receive_progress_ms = protocol_now_ms(&p2->link);

        if (p2->hooks.drop_data_ack != NULL && p2->hooks.drop_data_ack(block_index))
        {
            return;
        }

        pico2_ack(p2, frame);

        protocol_log(&p2->link, "[P2] DATA block received and ACKed\n");
        return;
    }

    // Duplicate of the most recently accepted block.
    if (p2->expected_block > 0 &&
        block_index == (uint16_t)(p2->expected_block - 1) &&
        frame->sequence == p2->last_data_sequence &&
        data_length == p2->last_data_length &&
        memcmp(&p2->received_log[p2->received_log_length - data_length],
               &frame->payload[DATA_BLOCK_INDEX_SIZE], data_length) == 0)
    {
        protocol_log(&p2->link, "[P2] Duplicate DATA block detected; ACKing without storing twice\n");

        pico2_ack(p2, frame);

        return;
    }

    // Unexpected block order.
    pico2_nack(p2, frame, NACK_BAD_BLOCK);
}

static void pico2_handle_transfer_end(pico2_ctx_t *p2,
                                      const protocol_frame_t *frame)
{
    // A lost END ACK must not re-run completion or disturb a newer command.
    if (p2->completed_end_valid &&
        frame->payload_length == TRANSFER_END_PAYLOAD_SIZE &&
        frame->sequence == p2->completed_end_sequence &&
        read_u16_le(frame->payload) == p2->completed_end_blocks)
    {
        pico2_ack(p2, frame);
        return;
    }
    if (p2->state != P2_RECEIVING_DATA)
    {
        return;
    }
    if (frame->payload_length != TRANSFER_END_PAYLOAD_SIZE)
    {
        pico2_nack(p2, frame, NACK_BAD_PAYLOAD);
        return;
    }

    uint16_t total_blocks = read_u16_le(frame->payload);

    bool transfer_valid = total_blocks == p2->expected_block;
    if (transfer_valid && p2->hooks.validate_log != NULL)
    {
        transfer_valid = p2->hooks.validate_log(p2->received_log, p2->received_log_length);
    }

    if (!transfer_valid)
    {
        pico2_nack(p2, frame, NACK_BAD_BLOCK);

        protocol_log(&p2->link, "[P2] Transfer validation failed\n");
        pico2_abort(p2);
        return;
    }

    pico2_ack(p2, frame);

    protocol_log(&p2->link, "[P2] TRANSFER_END validated\n");
    p2->completed_end_valid = true;
    p2->completed_end_sequence = frame->sequence;
    p2->completed_end_blocks = total_blocks;
    p2->received_log_valid = true;
    p2->state = P2_COMPLETE;
    if (p2->hooks.transfer_complete != NULL)
    {
        p2->hooks.transfer_complete(p2);
    }
}

void pico2_handle_frame(pico2_ctx_t *p2, const protocol_frame_t *frame)
{
    if (frame->session_id != p2->link.session_id)
    {
        return;
    }
    check_receive_timeout(p2);
    switch (frame->message_type)
    {
        case MSG_ACK:          pico2_handle_ack(p2, frame); break;
        case MSG_NACK:         pico2_handle_nack(p2, frame); break;
        case MSG_DATA:         pico2_handle_data(p2, frame); break;
        case MSG_TRANSFER_END: pico2_handle_transfer_end(p2, frame); break;
        default: break;
    }
}

void pico2_tick(pico2_ctx_t *p2)
{
    check_receive_timeout(p2);
    if (p2->state == P2_FAILED)
    {
        return;
    }
    if (!pending_timed_out(&p2->link, &p2->pending_command))
    {
        return;
    }

    if (!retry_pending(&p2->link, &p2->pending_command))
    {
        protocol_log(&p2->link, "\n[P2] Command timeout: retry limit reached\n");
        pico2_abort(p2);
    }
    else
    {
        protocol_log(&p2->link, "\n[P2] Command timeout: retransmitted\n");
    }
}
