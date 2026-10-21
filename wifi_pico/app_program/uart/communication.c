#include "app.h"
#include "ui_logic.h"
#include "protocol_io.h"
#include "pico2/pico2_protocol.h"
#include "demo/mock_logger.h"
#include <string.h>

static pico2_ctx_t p2;
static stream_parser_t parser;
static uint8_t received[APP_RECEIVE_CAPACITY];
static unsigned operation, configured, completions;
static bool expected_empty;
static unsigned long long indication_until;

static bool check_test_log(const uint8_t *bytes, size_t length)
{
    /* Test oracle belongs to this single-board harness, not the protocol. */
    return expected_empty ? length == 0 : mock_logger_matches(bytes, length);
}
static void completed(pico2_ctx_t *context)
{
    ++completions;
    rt_numbers("[TEST] Completed log matches emulator: bytes=%u completion_count=%u\n",
               (unsigned)context->received_log_length, completions, 0);
}
static void handle_frame(const protocol_frame_t *frame)
{
    bool duplicate_end = p2.completed_end_valid && frame->message_type == MSG_TRANSFER_END &&
        frame->sequence == p2.completed_end_sequence &&
        frame->payload_length == TRANSFER_END_PAYLOAD_SIZE &&
        read_u16_le(frame->payload) == p2.completed_end_blocks;
    fault_observe_gateway(frame);
    pico2_handle_frame(&p2, frame);
    if (duplicate_end) {
        rt_numbers("[P2] Repeated TRANSFER_END ACKed; completion_count=%u\n", completions, 0, 0);
    }
}
static void indicate(unsigned kind)
{
    app_message_t message = {.kind = kind, .at_ms = rt_now_ms()};
    indication_until = message.at_ms + app_flash_duration(kind) + 10u;
    if (!rt_status_send(&message)) {
        rt_log("[STATUS] Queue full; indication dropped\n");
    }
}
void communication_init(void)
{
    operation = configured = completions = 0;
    expected_empty = false;
    indication_until = 0;
    stream_parser_init(&parser);
    if (!pico2_init(&p2, (protocol_link_t){uart0, 0x11223344u, rt_log},
                    received, sizeof(received), 1700000000u, 3600u,
                    (pico2_hooks_t){.validate_log = check_test_log, .transfer_complete = completed})) {
        rt_fatal("pico2_init", -1);
    }
    p2.state = P2_COMPLETE; /* Same agreed session as the verified bare-metal UI. */
}
communication_snapshot_t communication_snapshot(void)
{
    return (communication_snapshot_t){p2.state, p2.received_log_valid,
        (unsigned)p2.received_log_length, completions, configured};
}
static void update_result(void)
{
    if (!operation) { return; }
    if (p2.state == P2_FAILED) {
        if (operation == APP_CONFIG) { rt_log("[UI] CONFIG failed\n"); }
        if (operation == APP_RETRIEVE) { rt_log("[UI] Data retrieval failed\n"); }
        if (operation == APP_CLEAR) { rt_log("[UI] CLEAR_LOG failed\n"); }
        rt_numbers("[RX] received_log_valid=%u received_bytes=%u\n",
                   p2.received_log_valid, (unsigned)p2.received_log_length, 0);
        operation = 0;
        indicate(APP_STATUS_FAILED);
    } else if (p2.state == P2_COMPLETE && !p2.pending_command.active) {
        if (operation == APP_CONFIG) {
            configured = 1;
            rt_log("[UI] CONFIG acknowledged\n");
            indicate(APP_STATUS_CONFIG);
        } else if (operation == APP_RETRIEVE && p2.received_log_valid) {
            rt_log("[UI] Data retrieval complete\n");
            indicate(APP_STATUS_RETRIEVE);
        } else if (operation == APP_CLEAR) {
            expected_empty = true;
            rt_log("[UI] Log cleared\n");
            indicate(APP_STATUS_CLEAR);
        }
        operation = 0;
    }
}
static void command(const app_message_t *message)
{
    if (operation || message->at_ms < indication_until) {
        rt_log("[UI] Busy; release button and press again when idle\n");
        return;
    }
    bool sent = false;
    if (message->kind == APP_CONFIG) {
        rt_log("[UI] Sending CONFIG\n");
        sent = pico2_send_config(&p2);
    } else if (!configured) {
        rt_log("[UI] Configure first using GP20\n");
        return;
    } else if (message->kind == APP_RETRIEVE) {
        rt_log("[UI] Requesting stored data\n");
        sent = pico2_request_data(&p2);
    } else if (message->kind == APP_CLEAR) {
        rt_log("[UI] Clearing stored log\n");
        sent = pico2_clear_log(&p2);
    }
    if (sent) { operation = message->kind; }
    else {
        rt_log("[UI] Command could not be started\n");
        indicate(APP_STATUS_FAILED);
    }
}
void communication_step(void)
{
    unsigned fault = protocol_receive(0, &parser, handle_frame);
    if (fault) {
        pico2_abort(&p2);
        rt_numbers("[UART0] RX/TX loss fault=%u; partial log discarded\n", fault, 0, 0);
    }
    pico2_tick(&p2);
    update_result();
    for (unsigned i = 0; i < APP_QUEUE_DEPTH; ++i) {
        app_message_t message;
        if (!rt_command_take(&message)) { break; }
        command(&message);
    }
}
void communication_run(void)
{
    int error = rt_uart_open(0);
    if (error) { rt_fatal("UART0 setup", error); }
    communication_init();
    rt_log("[READY] Communication Task: UART0 GP0/GP1, 115200 8N1\n");
    unsigned loops = 0;
    unsigned long long report = 0;
    for (;;) {
        communication_step();
        if (rt_now_ms() >= report) {
            rt_trace("Communication", ++loops, 0);
            report = rt_now_ms() + 5000u;
        }
        rt_wait(0, 10);
    }
}
