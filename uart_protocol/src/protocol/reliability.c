#include "reliability.h"
#include "frame.h"
#include "pico/stdlib.h"

static uint64_t now_ms(void)
{
    return time_us_64() / 1000u;
}

static bool send_frame(
    uart_inst_t *uart,
    uint8_t message_type,
    uint32_t session_id,
    uint16_t sequence,
    const uint8_t *payload,
    uint16_t payload_length,
    uint8_t *scratch,
    size_t scratch_size,
    size_t *encoded_length)
{
    *encoded_length = frame_encode(
        message_type,
        0x00,
        session_id,
        sequence,
        payload,
        payload_length,
        scratch,
        scratch_size
    );

    if (*encoded_length == 0)
    {
        return false;
    }

    uart_write_blocking(uart, scratch, *encoded_length);
    return true;
}

bool prepare_tracked_frame(
    uint8_t message_type,
    uint32_t session_id,
    uint16_t sequence,
    const uint8_t *payload,
    uint16_t payload_length,
    pending_tx_t *pending)
{
    size_t encoded_length = frame_encode(
        message_type,
        0x00,
        session_id,
        sequence,
        payload,
        payload_length,
        pending->frame,
        sizeof(pending->frame)
    );

    if (encoded_length == 0)
    {
        return false;
    }

    pending->active = true;
    pending->sequence = sequence;
    pending->frame_length = encoded_length;
    pending->sent_at_ms = now_ms();
    pending->retries = 0;

    return true;
}

bool send_tracked_frame(
    uart_inst_t *uart,
    uint8_t message_type,
    uint32_t session_id,
    uint16_t sequence,
    const uint8_t *payload,
    uint16_t payload_length,
    pending_tx_t *pending)
{
    if (!prepare_tracked_frame(message_type, session_id, sequence,
                               payload, payload_length, pending))
    {
        return false;
    }
    uart_write_blocking(uart, pending->frame, pending->frame_length);
    return true;
}

bool retry_pending(
    uart_inst_t *uart,
    pending_tx_t *pending)
{
    if (!pending->active)
    {
        return false;
    }

    if (pending->retries >= MAX_RETRIES)
    {
        return false;
    }

    uart_write_blocking(
        uart,
        pending->frame,
        pending->frame_length
    );

    pending->retries++;
    pending->sent_at_ms = now_ms();
    return true;
}

bool send_ack(
    uart_inst_t *uart,
    uint32_t session_id,
    uint16_t tx_sequence,
    uint16_t acknowledged_sequence,
    uint8_t *scratch,
    size_t scratch_size)
{
    uint8_t payload[ACK_PAYLOAD_SIZE];
    write_u16_le(payload, acknowledged_sequence);

    size_t encoded_length = 0;

    return send_frame(
        uart,
        MSG_ACK,
        session_id,
        tx_sequence,
        payload,
        sizeof(payload),
        scratch,
        scratch_size,
        &encoded_length
    );
}

bool send_nack(
    uart_inst_t *uart,
    uint32_t session_id,
    uint16_t tx_sequence,
    uint16_t rejected_sequence,
    uint8_t reason,
    uint8_t *scratch,
    size_t scratch_size)
{
    uint8_t payload[NACK_PAYLOAD_SIZE];
    write_u16_le(payload, rejected_sequence);
    payload[2] = reason;

    size_t encoded_length = 0;

    return send_frame(
        uart,
        MSG_NACK,
        session_id,
        tx_sequence,
        payload,
        sizeof(payload),
        scratch,
        scratch_size,
        &encoded_length
    );
}

bool pending_timed_out(const pending_tx_t *pending)
{
    return pending->active && now_ms() - pending->sent_at_ms >= ACK_TIMEOUT_MS;
}
