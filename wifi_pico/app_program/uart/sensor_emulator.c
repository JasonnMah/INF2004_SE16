/* TEST HARNESS ONLY. The separate real Sensor Pico remains bare-metal. */
#include "app.h"
#include "protocol_io.h"
#include "pico1/pico1_protocol.h"
#include "demo/mock_logger.h"
static pico1_ctx_t p1;
static stream_parser_t parser;
static void handle_frame(const protocol_frame_t *frame)
{
    fault_observe_sensor(frame);
    pico1_transfer_state_t previous = p1.transfer_state;
    pico1_handle_frame(&p1, frame);
    if (previous == P1_WAIT_END_ACK && p1.transfer_state == P1_TRANSFER_IDLE &&
        frame->message_type == MSG_ACK) {
        rt_log("[EMU] TRANSFER_END ACK received\n");
    }
}
void sensor_emulator_init(void)
{
    stream_parser_init(&parser);
    if (!pico1_init(&p1, (protocol_link_t){uart1, 0x11223344u, rt_log},
                    mock_logger_data(), mock_logger_length(), (pico1_hooks_t){0})) {
        rt_fatal("Sensor emulator init", -1);
    }
}
void sensor_emulator_step(void)
{
    if (protocol_receive(1, &parser, handle_frame)) {
        p1.pending_transfer.active = false;
        p1.transfer_state = P1_TRANSFER_IDLE;
        rt_log("[EMU] UART1 loss; transfer stopped, RAM log retained\n");
    }
    pico1_tick(&p1);
}
void sensor_emulator_run(void)
{
    int error = rt_uart_open(1);
    if (error) { rt_fatal("UART1 setup", error); }
    sensor_emulator_init();
    rt_log("[EMU] Sensor Emulator Task: UART1 GP4/GP5; immutable 100-byte RAM log\n");
    unsigned loops = 0;
    unsigned long long report = 0;
    for (;;) {
        sensor_emulator_step();
        if (rt_now_ms() >= report) {
            rt_trace("SensorEmulator(TEST)", ++loops, 1);
            report = rt_now_ms() + 5000u;
        }
        rt_wait(1, 10);
    }
}
