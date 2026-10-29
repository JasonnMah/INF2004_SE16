/* Explicit lab builds only. No changes to framing, protocol timing or Sensor firmware. */
#include "app.h"
#include "uart_io.h"
static bool first_ack, suppress_retrieval, saw_request, drop_end, end_dropped;
static uint16_t blocked_command, first_request, end_sequence;
void fault_reset(void)
{
    first_ack = suppress_retrieval = saw_request = drop_end = end_dropped = false;
    blocked_command = first_request = end_sequence = 0;
}
void fault_observe_sensor(const protocol_frame_t *frame)
{
    if (UART_FAULT != 2 || frame->message_type != MSG_REQUEST_DATA) { return; }
    if (!saw_request) {
        saw_request = true;
        first_request = frame->sequence;
        suppress_retrieval = true;
        rt_log("[TEST] Suppressing first retrieval DATA/END; REQUEST ACK is delivered\n");
    } else if (frame->sequence != first_request) {
        suppress_retrieval = false;
    }
}
void fault_observe_gateway(const protocol_frame_t *frame)
{
    if (UART_FAULT == 3 && !end_dropped && frame->message_type == MSG_TRANSFER_END) {
        end_sequence = frame->sequence;
        drop_end = true;
    }
}
bool fault_drop_rx(const protocol_frame_t *frame)
{
#if !APP_SENSOR_EMULATOR
    /* Filter only in the Communication Task, after CRC validation. ACKs remain
     * visible so the receiver really enters its existing receive-timeout state. */
    return UART_FAULT == 2 && suppress_retrieval &&
        frame->session_id == 0x11223344u &&
        (frame->message_type == MSG_DATA || frame->message_type == MSG_TRANSFER_END);
#else
    (void)frame;
    return false;
#endif
}
bool fault_drop_tx(unsigned unit, const uint8_t *bytes, size_t length)
{
    if (UART_FAULT == 0) { return false; }
    protocol_frame_t frame;
    if (!frame_decode(bytes, length, &frame)) { return false; }
#if !APP_SENSOR_EMULATOR
    if (UART_FAULT == 2 && unit == 0 && frame.message_type == MSG_REQUEST_DATA) {
        if (!saw_request) {
            saw_request = true;
            first_request = frame.sequence;
            suppress_retrieval = true;
            rt_log("[TEST] First retrieval armed: ignore DATA/END, accept REQUEST ACK\n");
        } else if (frame.sequence != first_request && suppress_retrieval) {
            suppress_retrieval = false;
            rt_log("[TEST] Suppression released; fresh retrieval receives normally\n");
        }
    }
#endif
    if (UART_FAULT == 1 && unit == 1 && frame.message_type == MSG_ACK &&
        frame.payload_length == ACK_PAYLOAD_SIZE) {
        uint16_t ack = read_u16_le(frame.payload);
        if (!first_ack) { first_ack = true; blocked_command = ack; }
        if (ack == blocked_command) {
            rt_log("[TEST] Dropping ACK for first CONFIG command sequence\n");
            return true;
        }
    }
    if (APP_SENSOR_EMULATOR && UART_FAULT == 2 && unit == 1 && suppress_retrieval &&
        (frame.message_type == MSG_DATA || frame.message_type == MSG_TRANSFER_END)) {
        return true;
    }
    if (UART_FAULT == 3 && unit == 0 && drop_end && !end_dropped &&
        frame.message_type == MSG_ACK && frame.payload_length == ACK_PAYLOAD_SIZE &&
        read_u16_le(frame.payload) == end_sequence) {
        end_dropped = true;
        drop_end = false;
        rt_log("[TEST] Dropping first TRANSFER_END ACK only\n");
        return true;
    }
    return false;
}
