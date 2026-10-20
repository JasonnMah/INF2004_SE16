#include "demo.h"
#include "failure_injection.h"

static void demo_transfer_complete(pico2_ctx_t *p2)
{
    protocol_log(&p2->link, "[P2] Retrieved compressed log matches Pico 1 data\n");
    // Automatic CLEAR_LOG is a demo action; a real app chooses when to clear.
    if (!pico2_send_command(p2, MSG_CLEAR_LOG, NULL, 0, P2_WAIT_CLEAR_ACK))
    {
        p2->state = P2_FAILED;
    }
}

bool demo_init(pico1_ctx_t *p1, pico2_ctx_t *p2,
               protocol_link_t sensor, protocol_link_t receiver,
               uint8_t *receive_buffer, size_t receive_capacity)
{
    failure_injection_reset(receiver.log);
    pico1_hooks_t sensor_hooks = {
        .send_data = failure_send_data,
        .drop_config_ack = failure_drop_config_ack
    };
    pico2_hooks_t receiver_hooks = {
        .drop_data_ack = failure_drop_data_ack,
        .validate_log = mock_logger_matches,
        .transfer_complete = demo_transfer_complete
    };
    return pico1_init(p1, sensor, mock_logger_data(), mock_logger_length(), sensor_hooks) &&
           pico2_init(p2, receiver, receive_buffer, receive_capacity,
                       1700000000u, 3600u, receiver_hooks);
}

void demo_start(pico2_ctx_t *p2)
{
    failure_print_banner();
    if (!pico2_send_command(p2, MSG_HELLO, NULL, 0, P2_WAIT_HELLO_ACK))
    {
        p2->state = P2_FAILED;
    }
}

void demo_report_result(pico2_ctx_t *p2, bool *reported)
{
    if (p2->state == P2_FAILED)
    {
        protocol_log(&p2->link, "\n=== UART PROTOCOL FAILURE TEST FAILED ===\n");
        p2->state = P2_COMPLETE;
        *reported = true;
    }
    else if (p2->state == P2_COMPLETE && !*reported)
    {
        protocol_log(&p2->link, "\n=== UART PROTOCOL FAILURE TEST COMPLETE ===\n");
        *reported = true;
    }
}
