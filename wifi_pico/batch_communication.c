/* Core-0 Communication task owns both UART endpoints and the batch journal.
 * Only copied descriptors cross to the radio owner. HTTP borrows the immutable
 * 4 KiB body until its result arrives; buttons never modify that body. */
#include "app.h"
#include "buttons.h"
#include "uart_io.h"
#include "batch_gateway.h"
#include "flash_port.h"
#include "upload.h"
#include "pc_config.h"
#include <string.h>

static batch_gateway_t gateway;
static pico2_ctx_t commands;
static uint8_t unused_receive[1];
static stream_parser_t parser;
static unsigned operation, fetching, token, configured, completions, retry_reported;
static unsigned long long indication_until, next_activity;

static void indicate(unsigned kind)
{
    app_message_t message = {.kind = kind, .at_ms = rt_now_ms()};
    indication_until = message.at_ms + app_flash_duration(kind) + 10;
    (void)rt_status_send(&message);
}
static bool queue_upload(void *context, const uint8_t *bytes, unsigned length,
                         const char *id, unsigned total)
{
    (void)context;
    if (!upload_network_ready()) return false;
    upload_job_t job = {.data=bytes,.length=length,.id=id,.total_length=total};
    return rt_upload_send(&job) != 0;
}
static void batch_event(void *context, unsigned kind, uint32_t offset, uint32_t total)
{
    (void)context;
    if (kind == BG_EVENT_SAVED) {
        rt_numbers("[BATCH] Saved %u bytes to Wi-Fi flash; offset=%u total=%u\n",
                   gateway.store.record.length, offset, total);
    } else if (kind == BG_EVENT_CONFIRMED) {
        rt_numbers("[BATCH] PC confirmed %u / %u bytes; temporary flash bytes released\n",
                   offset + gateway.store.record.length, total, 0);
        retry_reported = 0;
    } else if (kind == BG_EVENT_COMPLETE) {
        ++completions;
        rt_numbers("[UI] Transfer complete: %u bytes stored on PC; Sensor flash retained\n",total,0,0);
        indicate(APP_STATUS_UPLOAD);
        retry_reported = 0;
    } else if (kind == BG_EVENT_RETRY && !retry_reported) {
        rt_log("[BATCH] Transfer paused; data retained. Retrying automatically after 5 seconds\n");
        indicate(APP_STATUS_FAILED);
        retry_reported = 1;
    }
}
static void quiet_log(const char *text)
{
    /* Print batch summaries, rather than one line for each eight-byte frame. */
    if (strstr(text, "DATA block") || strstr(text, "REQUEST_DATA acknowledged") ||
        strstr(text, "TRANSFER_END validated")) return;
    rt_log(text);
}
static void frame(const protocol_frame_t *value)
{
    batch_gateway_frame(&gateway, value);
    if (operation) pico2_handle_frame(&commands, value);
}
void communication_init(void)
{
    operation = fetching = token = configured = completions = retry_reported = 0;
    indication_until = next_activity = 0;
    batch_flash_t flash;
    protocol_link_t link = app_protocol_link(0);
    link.log = quiet_log;
    stream_parser_init(&parser);
    if (!flash_port_init(&flash) || !batch_gateway_init(&gateway, link, flash,
            (batch_gateway_io_t){.upload=queue_upload,.event=batch_event})) {
        rt_fatal("Batch flash journal invalid; preserved for investigation", -1);
    }
    if (!pico2_init(&commands, link, unused_receive, sizeof(unused_receive),0,60,
                    (pico2_hooks_t){0})) rt_fatal("Command endpoint", -1);
    commands.state = P2_COMPLETE;
    commands.clear_ack_timeout_ms = STORAGE_CLEAR_ACK_TIMEOUT_MS;
    if (batch_gateway_active(&gateway)) {
        rt_numbers("[RECOVERY] Resuming saved transfer: state=%u offset=%u total=%u\n",
                   gateway.store.record.state,gateway.store.record.offset,gateway.store.record.total);
    }
}
communication_snapshot_t communication_snapshot(void)
{
    return (communication_snapshot_t){gateway.state, gateway.state == BG_IDLE,
        gateway.store.record.length, completions, configured};
}
static void pc_result(void)
{
    pc_config_result_t result;
    if (!rt_config_result_take(&result) || !fetching || result.token != token) return;
    fetching = 0;
    if (result.result <= 0 || result.interval_minutes < 1 || result.interval_minutes > 4095 ||
        !result.unix_seconds) {
        rt_log("[UI] CONFIG not sent: PC settings unavailable or invalid; retry GP20\n");
        indicate(APP_STATUS_FAILED); return;
    }
    commands.config_timestamp = result.unix_seconds;
    commands.config_interval = result.interval_minutes * 60;
    rt_numbers("[CONFIG] PC interval=%u minutes (%u seconds); timestamp=%u\n",
               result.interval_minutes,commands.config_interval,commands.config_timestamp);
    if (pico2_send_config(&commands)) operation = APP_CONFIG;
    else indicate(APP_STATUS_FAILED);
}
static void command_result(void)
{
    if (!operation) return;
    if (commands.state == P2_FAILED) {
        rt_log("[UI] Sensor command failed; stored logs retained\n");
        operation = 0; indicate(APP_STATUS_FAILED);
    } else if (commands.state == P2_COMPLETE && !commands.pending_command.active) {
        if (operation == APP_CONFIG) {
            configured = 1; rt_log("[UI] CONFIG acknowledged\n"); indicate(APP_STATUS_CONFIG);
        } else {
            rt_log("[UI] Sensor flash cleared and verified\n"); indicate(APP_STATUS_CLEAR);
        }
        operation = 0;
    }
}
static void command(const app_message_t *message)
{
    if (operation || fetching || batch_gateway_active(&gateway) ||
        message->at_ms < indication_until) {
        rt_log("[UI] Busy; automatic transfer/recovery continues. Button ignored\n"); return;
    }
    if (message->kind == APP_CONFIG) {
        if (++token == 0) ++token;
        if (rt_config_request_send(token)) {
            fetching = 1; rt_log("[UI] Reading interval and time from PC\n");
        } else indicate(APP_STATUS_FAILED);
    } else if (message->kind == APP_RETRIEVE) {
        /* Flash may contain earlier readings after either board restarts.
         * Retrieving retained data does not require a new CONFIG. */
        rt_log("[UI] Starting automatic retrieval/upload; one press transfers the whole log\n");
        if (!batch_gateway_start(&gateway)) indicate(APP_STATUS_FAILED);
    } else if (message->kind == APP_CLEAR) {
        rt_log("[UI] Clearing Sensor flash; waiting for verified completion\n");
        if (pico2_clear_log(&commands)) operation = APP_CLEAR;
        else indicate(APP_STATUS_FAILED);
    }
}
static void activity(void)
{
    if (gateway.state == BG_FAULT) return;
    unsigned long long now = rt_now_ms();
    if (now < indication_until || now < next_activity) return;
    unsigned active = fetching || operation || batch_gateway_active(&gateway);
    if (!active) return;
    unsigned loading = fetching || operation || gateway.state == BG_RETRY ||
        (gateway.state == BG_UPLOAD && !upload_network_ready());
    app_message_t status = {.kind=loading?APP_STATUS_LOADING:APP_STATUS_TRANSFERRING,.at_ms=now};
    (void)rt_status_send(&status);
    next_activity = now + APP_TRANSFER_HEARTBEAT_MS;
}
void communication_step(void)
{
    /* Results end the radio's buffer loan before any recovery may reuse bytes. */
    int result = rt_upload_result_take();
    if (result) batch_gateway_upload_result(&gateway, result);
    if (protocol_receive(0, &parser, frame)) {
        batch_gateway_rx_fault(&gateway);
        if (operation) pico2_abort(&commands);
        rt_log("[UART] Receive fault; original log retained for retry\n");
    }
    if (operation) pico2_tick(&commands);
    command_result(); pc_result(); batch_gateway_tick(&gateway);
    if (gateway.state == BG_FAULT && !retry_reported) {
        rt_log("[BATCH] Flash journal/data verification failed; cache preserved. Transfer stopped\n");
        retry_reported = 1; indicate(APP_STATUS_FAILED);
    }
    for (unsigned i=0; i<APP_QUEUE_DEPTH; ++i) {
        app_message_t message;
        if (!rt_command_take(&message)) break;
        command(&message);
    }
    activity();
}
void communication_run(void)
{
    int error=rt_uart_open(0);
    if (error) rt_fatal("UART0 setup", error);
    communication_init();
    rt_log("[READY] Batched Communication Task: UART0 GP0/GP1, 115200 8N1\n");
    unsigned reports=0;
    unsigned long long report=0;
    for (;;) {
        communication_step();
        if (rt_now_ms() >= report) { rt_trace("Communication",++reports,0);report=rt_now_ms()+5000; }
        rt_wait(0,10);
    }
}
