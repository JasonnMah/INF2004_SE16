#include "app.h"
#include "buttons.h"
#include "uart_io.h"
#include "pico2/pico2_protocol.h"
#if APP_SENSOR_EMULATOR
#include "demo/mock_logger.h"
#else
#include "lab_capture.h"
#endif
#include <string.h>
#if APP_COMBINED
#include "upload.h"
#include "pc_config.h"
static unsigned config_fetching, config_token;
#endif

static pico2_ctx_t p2;
static stream_parser_t parser;
static uint8_t received[APP_RECEIVE_CAPACITY];
static unsigned operation, configured, completions;
#if APP_SENSOR_EMULATOR
static bool expected_empty;
#endif
static unsigned long long indication_until, next_activity_status;

#if APP_SENSOR_EMULATOR
static bool check_test_log(const uint8_t *bytes, size_t length)
{
    /* Test oracle belongs to this single-board harness, not the protocol. */
    return expected_empty ? length == 0 : mock_logger_matches(bytes, length);
}
#endif
static void completed(pico2_ctx_t *context)
{
    ++completions;
#if APP_COMBINED
    log_workflow_completed(context->received_log, (unsigned)context->received_log_length);
#endif
#if APP_SENSOR_EMULATOR
    rt_numbers("[TEST] Completed log matches emulator: bytes=%u completion_count=%u\n",
               (unsigned)context->received_log_length, completions, 0);
#else
    rt_numbers("[RX] received_log_valid=1 received_bytes=%u blocks=%u completion_count=%u\n",
               (unsigned)context->received_log_length, context->expected_block, completions);
    lab_capture_begin(context->received_log, (unsigned)context->received_log_length, completions);
#endif
}
static void handle_frame(const protocol_frame_t *frame)
{
    bool duplicate_end = frame->session_id == p2.link.session_id && p2.completed_end_valid &&
        frame->message_type == MSG_TRANSFER_END &&
        frame->sequence == p2.completed_end_sequence &&
        frame->payload_length == TRANSFER_END_PAYLOAD_SIZE &&
        read_u16_le(frame->payload) == p2.completed_end_blocks;
#if APP_FAULT_INJECTION
    if (fault_drop_rx(frame)) { return; }
    fault_observe_gateway(frame);
#endif
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
#if APP_COMBINED
    log_workflow_init();
    config_fetching = config_token = 0;
#endif
#if APP_SENSOR_EMULATOR
    expected_empty = false;
#else
    lab_capture_cancel();
#endif
    indication_until = next_activity_status = 0;
    stream_parser_init(&parser);
    if (!pico2_init(&p2, app_protocol_link(0),
                    received, sizeof(received), 1700000000u, 3600u,
                    (pico2_hooks_t){
#if APP_SENSOR_EMULATOR
                        .validate_log = check_test_log,
#endif
                        .transfer_complete = completed})) {
        rt_fatal("pico2_init", -1);
    }
#if !APP_SENSOR_EMULATOR
    /* Physical 1 MiB erase is completion-ACKed, unlike the RAM demo clear.
     * Only this opt-in command gets a longer ACK window; CONFIG/DATA stay 500 ms. */
    p2.clear_ack_timeout_ms = STORAGE_CLEAR_ACK_TIMEOUT_MS;
#endif
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
        rt_numbers("[RX] received_log_valid=%u received_bytes=%u completion_count=%u\n",
                   p2.received_log_valid, (unsigned)p2.received_log_length, completions);
#if !APP_SENSOR_EMULATOR
        rt_numbers("[CMD] sequence=%u retries=%u limit=3\n",
                   p2.pending_command.sequence, p2.pending_command.retries, 0);
#endif
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
#if APP_SENSOR_EMULATOR
            expected_empty = true;
#endif
#if APP_COMBINED
            log_workflow_cleared();
#endif
            rt_log("[UI] Log cleared\n");
            indicate(APP_STATUS_CLEAR);
        }
#if !APP_SENSOR_EMULATOR
        rt_numbers("[CMD] sequence=%u retries=%u limit=3\n",
                   p2.pending_command.sequence, p2.pending_command.retries, 0);
#endif
        operation = 0;
    }
}
#if APP_COMBINED
static void pc_configuration_result(void)
{
    pc_config_result_t result;
    if (!rt_config_result_take(&result)) return;
    if (!config_fetching || result.token != config_token) {
        rt_log("[CONFIG] Stale PC result ignored\n");
        return;
    }
    config_fetching = 0;
    if (result.result <= 0 || result.interval_minutes < 1 ||
        result.interval_minutes > 4095 || !result.unix_seconds) {
        rt_log("[UI] CONFIG not sent: PC settings unavailable or invalid. Save an interval on the PC, then retry GP20\n");
        indicate(APP_STATUS_FAILED);
        return;
    }
    p2.config_interval = result.interval_minutes * 60u;
    p2.config_timestamp = result.unix_seconds;
    rt_numbers("[CONFIG] PC interval=%u minutes (%u seconds); timestamp=%u\n",
               result.interval_minutes, p2.config_interval, p2.config_timestamp);
    rt_log("[UI] Sending CONFIG\n");
    if (pico2_send_config(&p2)) operation = APP_CONFIG;
    else {
        rt_log("[UI] Command could not be started\n");
        indicate(APP_STATUS_FAILED);
    }
}
#endif
static void command(const app_message_t *message)
{
    if (operation || message->at_ms < indication_until
#if APP_COMBINED
        || config_fetching
#endif
#if !APP_SENSOR_EMULATOR
        || lab_capture_active()
#endif
    ) {
        rt_log("[UI] Busy; release button and press again when idle\n");
        return;
    }
#if APP_COMBINED
    if (!log_workflow_command(message->kind, message->at_ms)) return;
#endif
    bool sent = false;
    if (message->kind == APP_CONFIG) {
#if APP_COMBINED
        if (++config_token == 0) ++config_token;
        if (!rt_config_request_send(config_token)) {
            rt_log("[UI] PC configuration queue busy; retry GP20\n");
            indicate(APP_STATUS_FAILED);
            return;
        }
        config_fetching = 1;
        rt_log("[UI] Reading interval and time from PC\n");
        return;
#else
        rt_log("[UI] Sending CONFIG\n");
        sent = pico2_send_config(&p2);
#endif
    } else if (!configured) {
        rt_log("[UI] Configure first using GP20\n");
        return;
    } else if (message->kind == APP_RETRIEVE) {
        rt_log("[UI] Requesting stored data\n");
        sent = pico2_request_data(&p2);
    } else if (message->kind == APP_CLEAR) {
        rt_log("[UI] Clearing stored log\n");
#if !APP_SENSOR_EMULATOR
        rt_numbers("[UI] Waiting for Sensor CLEAR acknowledgement: timeout=%u ms, max retries=3\n",
                   p2.clear_ack_timeout_ms, 0, 0);
#endif
        sent = pico2_clear_log(&p2);
    }
    if (sent) { operation = message->kind; }
    else {
        rt_log("[UI] Command could not be started\n");
        indicate(APP_STATUS_FAILED);
    }
}
static void activity_status(void)
{
    unsigned kind = APP_STATUS_TRANSFERRING;
    unsigned active = operation == APP_RETRIEVE;
#if APP_COMBINED
    active |= log_workflow_state() == 2; /* Queued/in-progress HTTP upload. */
    if (config_fetching || operation == APP_CONFIG) {
        active = 1;
        kind = APP_STATUS_LOADING; /* Silent, slower blink through fetch and UART retries. */
    }
#endif
    if (!active) {
        next_activity_status = 0;
        return;
    }
    unsigned long long now = rt_now_ms();
    if (now < next_activity_status) return;
    app_message_t message = {.kind = kind, .at_ms = now};
    if (!rt_status_send(&message)) rt_log("[STATUS] Queue full; activity indication dropped\n");
    next_activity_status = now + APP_TRANSFER_HEARTBEAT_MS;
}
void communication_step(void)
{
    unsigned fault = protocol_receive(0, &parser, handle_frame);
    if (fault) {
        pico2_abort(&p2);
#if !APP_SENSOR_EMULATOR
        lab_capture_cancel();
#endif
        rt_numbers("[UART0] RX/TX loss fault=%u; partial log discarded\n", fault, 0, 0);
    }
    pico2_tick(&p2);
    update_result();
#if APP_COMBINED
    log_workflow_poll();
    pc_configuration_result();
#endif
#if !APP_SENSOR_EMULATOR
    /* One bounded diagnostic line per poll; keep servicing RX/retries. */
    lab_capture_step();
#endif
    for (unsigned i = 0; i < APP_QUEUE_DEPTH; ++i) {
        app_message_t message;
        if (!rt_command_take(&message)) { break; }
        command(&message);
    }
    activity_status();
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
