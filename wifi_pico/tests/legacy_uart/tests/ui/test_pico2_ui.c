// Host integration checks: real UI, codec, parser, peers and reliability;
// only GPIO, UART byte transport and the clock are substituted.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pico2/pico2_ui.h"
#include "pico1/pico1_protocol.h"
#include "protocol/stream_parser.h"
#include "demo/mock_logger.h"

#define SESSION 0x11223344u
static uart_inst_t receiver_uart = {0}, sensor_uart = {1};
static pico1_ctx_t p1;
static pico2_ctx_t p2;
static pico2_ui_t ui;
static uint8_t receive_buffer[256];
static uint64_t clock_ms;
static bool levels[30], initialized[30], outputs[30], pulled_up[30];
static unsigned led_rises;
static char messages[8192];
static unsigned drops_remaining;
static bool drop_data;
static unsigned requests, data_frames, clears;
static struct {
    unsigned sender;
    uint8_t bytes[MAX_STUFFED_FRAME_SIZE];
    size_t length;
} sent[256];
static size_t sent_count, delivered;

uint64_t time_us_64(void) { return clock_ms * 1000; }
void gpio_init(unsigned pin)
{
    assert(pin == 6 || pin == 20 || pin == 21 || pin == 22);
    initialized[pin] = true;
}
void gpio_set_dir(unsigned pin, bool output) { outputs[pin] = output; }
void gpio_pull_up(unsigned pin) { pulled_up[pin] = true; }
bool gpio_get(unsigned pin) { assert(pin == 20 || pin == 21 || pin == 22); return levels[pin]; }
void gpio_put(unsigned pin, bool value)
{
    assert(pin == 6);
    if (!levels[pin] && value) { led_rises++; }
    levels[pin] = value;
}
static void log_text(const char *text)
{
    assert(strlen(messages) + strlen(text) < sizeof(messages));
    strcat(messages, text);
}
void uart_write_blocking(uart_inst_t *uart, const uint8_t *bytes, size_t length)
{
    assert(sent_count < 256 && length <= MAX_STUFFED_FRAME_SIZE);
    sent[sent_count].sender = uart->id;
    sent[sent_count].length = length;
    memcpy(sent[sent_count].bytes, bytes, length);
    sent_count++;
}
static unsigned count_message(const char *text)
{
    unsigned count = 0;
    const char *at = messages;
    while ((at = strstr(at, text)) != NULL) { count++; at += strlen(text); }
    return count;
}
static void transfer_failed(void)
{
    if (p2.state == P2_RECEIVING_DATA) { pico2_abort(&p2); }
}
static bool validate_log(const uint8_t *data, size_t length)
{
    return length == p1.logger_data_length &&
           (length == 0 || memcmp(data, p1.logger_data, length) == 0);
}
static void transfer_complete(pico2_ctx_t *receiver)
{
    protocol_log(&receiver->link, receiver->received_log_length == 0
        ? "[P2] Retrieved log is empty (0 bytes)\n"
        : "[P2] Retrieved compressed log matches Pico 1 data\n");
}
static void init(void)
{
    memset(levels, 0, sizeof(levels));
    memset(initialized, 0, sizeof(initialized));
    memset(outputs, 0, sizeof(outputs));
    memset(pulled_up, 0, sizeof(pulled_up));
    levels[20] = levels[21] = levels[22] = true;
    drop_data = false;
    requests = data_frames = clears = 0;
    led_rises = drops_remaining = 0;
    sent_count = delivered = 0;
    clock_ms = 0;
    messages[0] = '\0';
    assert(pico1_init(&p1, (protocol_link_t){&sensor_uart, SESSION, log_text},
                      mock_logger_data(), mock_logger_length(),
                      (pico1_hooks_t){.transfer_failed = transfer_failed}));
    assert(pico2_init(&p2, (protocol_link_t){&receiver_uart, SESSION, log_text},
                      receive_buffer, sizeof(receive_buffer), 1700000000u,
                      3600u, (pico2_hooks_t){.validate_log = validate_log,
                                               .transfer_complete = transfer_complete}));
    p2.state = P2_COMPLETE;
    pico2_ui_init(&ui, &p2);
    assert(initialized[6] && initialized[20] && outputs[6] && !outputs[20]);
    assert(pulled_up[20] && initialized[21] && pulled_up[21] && !outputs[21] && initialized[22] && pulled_up[22] && !outputs[22]);
}

static void deliver(void)
{
    while (delivered < sent_count)
    {
        size_t n = delivered++;
        stream_parser_t parser;
        stream_parser_init(&parser);
        protocol_frame_t frame;
        bool complete = false;
        for (size_t i = 0; i < sent[n].length; i++)
        {
            if (stream_parser_feed(&parser, sent[n].bytes[i]))
            {
                assert(frame_decode(parser.buffer, parser.length, &frame));
                complete = true;
            }
        }
        assert(complete);
        if (sent[n].sender == 0)
        {
            // HELLO must not start automatically; CLEAR_LOG requires a UI request.
            assert(frame.message_type == MSG_CONFIG || frame.message_type == MSG_REQUEST_DATA ||
                   frame.message_type == MSG_ACK || frame.message_type == MSG_NACK ||
                   frame.message_type == MSG_CLEAR_LOG);
            if (frame.message_type == MSG_CONFIG)
            {
                assert(frame.payload_length == CONFIG_PAYLOAD_SIZE);
                assert(read_u32_le(frame.payload) == 1700000000u);
                assert(read_u32_le(&frame.payload[CONFIG_INTERVAL_OFFSET]) == 3600u);
            }
            if (frame.message_type == MSG_REQUEST_DATA)
            {
                requests++;
                assert(frame.payload_length == REQUEST_DATA_PAYLOAD_SIZE);
                assert(read_u16_le(frame.payload) == 0);
            }
            if (frame.message_type == MSG_CLEAR_LOG)
            {
                assert(ui.clear_pending && frame.payload_length == 0);
                clears++;
            }
            pico1_handle_frame(&p1, &frame);
        }
        else
        {
            if (frame.message_type == MSG_DATA)
            {
                data_frames++;
                if (drop_data) { continue; }
            }
            if (frame.message_type == MSG_ACK && drops_remaining > 0) { drops_remaining--; }
            else { pico2_handle_frame(&p2, &frame); }
        }
    }
}

static void step(uint64_t now)
{
    clock_ms = now;
    deliver();
    pico1_tick(&p1);
    if ((ui.config_pending || ui.retrieval_pending || ui.clear_pending) && p2.state != P2_FAILED) { pico2_tick(&p2); }
    pico2_ui_tick(&ui);
}

static void test_press_and_success(void)
{
    init();
    step(0);
    assert(sent_count == 0); // Idle startup must not send a protocol command.
    levels[20] = false; step(1);
    levels[20] = true; step(5);
    levels[20] = false; step(10);
    step(39); assert(sent_count == 0);
    step(40); assert(sent_count == 1 && ui.config_pending && !levels[6]);
    assert(!pico2_send_config(&p2)); // A busy request must not overwrite sequence.
    step(41);
    assert(p1.configured && p1.sampling_interval == 3600u);
    assert(p2.state == P2_COMPLETE && !ui.config_pending && sent_count == 2);
    assert(levels[6] && count_message("[UI] CONFIG acknowledged") == 1);
    step(240); assert(levels[6]);
    step(241); assert(!levels[6]);
    step(5000); assert(sent_count == 2); // Holding for seconds still prints once.
    levels[20] = true; step(5010);
    levels[20] = false; step(5020); step(5050);
    assert(sent_count == 2); // Release bounce cannot re-arm.
    levels[20] = true; step(5060); step(5090);
    levels[20] = false; step(5100); step(5130); step(5131);
    assert(sent_count == 4 && count_message("[UI] CONFIG acknowledged") == 2);
}

static void test_retry_and_busy_press(void)
{
    init();
    drops_remaining = 1;
    levels[20] = false; step(0); step(30); step(31);
    assert(ui.config_pending && !levels[6]);
    levels[20] = true; step(40); step(70);
    levels[20] = false; step(80); step(110);
    assert(count_message("[UI] Busy;") == 1 && sent_count == 2);
    step(529); assert(p2.pending_command.retries == 0);
    step(530); assert(p2.pending_command.retries == 1);
    step(531);
    assert(sent_count == 4 && !ui.config_pending && levels[6]);
    assert(count_message("[UI] CONFIG acknowledged") == 1);
}

static void test_failure_and_recovery(void)
{
    init();
    drops_remaining = 100;
    levels[20] = false; step(0); step(30); step(31);
    step(530); step(1030); step(1530); step(2030);
    assert(p2.pending_command.retries == MAX_RETRIES && sent_count == 8);
    assert(p2.state == P2_FAILED && !ui.config_pending && led_rises == 1);
    assert(count_message("[UI] CONFIG failed") == 1);
    assert(count_message("[UI] CONFIG acknowledged") == 0);
    step(2129); assert(levels[6]);
    step(2130); assert(!levels[6]);
    step(2230); assert(levels[6]);
    step(2330); assert(!levels[6]);
    step(2430); assert(levels[6]);
    step(2530); assert(!levels[6] && led_rises == 3);
    step(6000);
    assert(sent_count == 8 && count_message("[UI] CONFIG failed") == 1);

    // Restore the link. A fresh press starts a new sequence/retry budget.
    drops_remaining = 0;
    levels[20] = true; step(6010); step(6040);
    levels[20] = false; step(6050); step(6080); step(6081);
    assert(p2.state == P2_COMPLETE && p2.pending_command.retries == 0);
    assert(sent_count == 10 && count_message("[UI] CONFIG acknowledged") == 1);
}

static void test_ack_validation_and_nack(void)
{
    init();
    levels[20] = false; step(0); step(30);
    protocol_frame_t reply = {.version = PROTOCOL_VERSION, .message_type = MSG_ACK,
        .session_id = SESSION, .payload_length = ACK_PAYLOAD_SIZE};
    write_u16_le(reply.payload, p2.pending_command.sequence + 1);
    pico2_handle_frame(&p2, &reply);
    assert(p2.pending_command.active);
    write_u16_le(reply.payload, p2.pending_command.sequence);
    reply.session_id++;
    pico2_handle_frame(&p2, &reply);
    assert(p2.pending_command.active);
    reply.session_id = SESSION;
    reply.payload_length = 1;
    pico2_handle_frame(&p2, &reply);
    assert(p2.pending_command.active);
    reply.message_type = MSG_NACK;
    reply.payload_length = NACK_PAYLOAD_SIZE;
    reply.payload[2] = NACK_BAD_PAYLOAD;
    pico2_handle_frame(&p2, &reply);
    assert(p2.pending_command.retries == 1 && sent_count == 2);
    reply.message_type = MSG_ACK;
    reply.payload_length = ACK_PAYLOAD_SIZE;
    pico2_handle_frame(&p2, &reply);
    pico2_ui_tick(&ui);
    assert(p2.state == P2_COMPLETE && count_message("[UI] CONFIG acknowledged") == 1);
    pico2_handle_frame(&p2, &reply);
    pico2_ui_tick(&ui);
    assert(count_message("[UI] CONFIG acknowledged") == 1 && sent_count == 2);
}

static void press(unsigned pin, uint64_t now)
{
    levels[pin] = false; step(now); step(now + 30);
}
static void release(unsigned pin, uint64_t now)
{
    levels[pin] = true; step(now); step(now + 30);
}
static void configured(void)
{
    init();
    press(20, 0); step(31); release(20, 40); step(231);
    assert(ui.configured);
}
static void test_retrieval(void)
{
    init();
    press(21, 0);
    assert(sent_count == 0 && !levels[6] && p2.state == P2_COMPLETE);
    assert(count_message("[UI] Configure first using GP20") == 1);
    step(1000); assert(sent_count == 0);
    release(21, 1010);
    press(20, 1100); step(1131); release(20, 1140); step(1331);
    assert(ui.configured);
    press(21, 1400);
    assert(ui.retrieval_pending && p2.state == P2_WAIT_REQUEST_ACK);
    assert(!pico2_request_data(&p2));
    step(1431);
    assert(!ui.retrieval_pending && p2.state == P2_COMPLETE);
    assert(requests == 1 && data_frames == 13 && p2.expected_block == 13);
    assert(p2.received_log_length == 100 && mock_logger_matches(receive_buffer, 100));
    assert(p1.logger_data_length == 100 && p1.transfer_state == P1_TRANSFER_IDLE);
    assert(count_message("[UI] Data retrieval complete") == 1);
    assert(count_message("[P2] Retrieved compressed log matches Pico 1 data") == 1);
    assert(levels[6]); step(1580); assert(levels[6]);
    step(1581); assert(!levels[6]); step(1681); assert(levels[6]);
    step(1831); assert(!levels[6] && led_rises == 3);
    step(5000); assert(requests == 1);
    // A bounced release does not re-arm GP21.
    levels[21] = true; step(5010); levels[21] = false; step(5020); step(5050);
    assert(requests == 1);
    release(21, 5060); press(21, 5100); step(5131);
    assert(clears == 0); // Retrieval never clears automatically.
    assert(requests == 2 && data_frames == 26 && p2.expected_block == 13);
    assert(mock_logger_matches(receive_buffer, p2.received_log_length));
    assert(p1.logger_data_length == 100 && count_message("[UI] Data retrieval complete") == 2);
}
static void test_retrieval_busy_and_failure(void)
{
    configured();
    drop_data = true;
    press(21, 300); step(331);
    assert(p2.state == P2_RECEIVING_DATA && ui.retrieval_pending);
    assert(!pico2_request_data(&p2));
    release(21, 340); press(21, 380); press(20, 420);
    press(22, 460);
    assert(clears == 0 && requests == 1 && count_message("[UI] Busy;") == 3);
    step(831); step(1331); step(1831); step(2331);
    assert(p1.pending_transfer.retries == MAX_RETRIES);
    assert(p2.state == P2_FAILED && !ui.retrieval_pending);
    assert(count_message("[UI] Data retrieval failed") == 1);
    step(2431); assert(!levels[6]); step(2531); assert(levels[6]);
    step(2631); step(2731); step(2831);
    assert(!levels[6] && led_rises == 4); // CONFIG plus three failure flashes.
    step(4000); assert(requests == 1); // Busy held buttons never become requests.
    drop_data = false;
    release(21, 4010); press(21, 4100); step(4131);
    assert(requests == 2 && mock_logger_matches(receive_buffer, p2.received_log_length));
    assert(count_message("[UI] Data retrieval complete") == 1);
}
static void test_gp21_during_config(void)
{
    init(); drops_remaining = 1;
    press(20, 0); step(31); press(21, 40);
    assert(requests == 0 && ui.config_pending && count_message("[UI] Busy;") == 1);
    press(22, 80);
    assert(clears == 0 && count_message("[UI] Busy;") == 2);
    step(530); step(531); step(731); step(1000);
    assert(ui.configured && requests == 0);
}

static void test_remote_receive_timeout(void)
{
    configured();
    // Separate boards cannot use the demo's local sender-failure callback.
    p1.hooks.transfer_failed = NULL;
    drop_data = true;
    press(21, 300); step(331); release(21, 340);
    assert(p2.state == P2_RECEIVING_DATA && ui.retrieval_pending);
    step(831); step(1331); step(1831); step(2331);
    assert(!p1.pending_transfer.active && p2.state == P2_RECEIVING_DATA);
    step(2830); assert(ui.retrieval_pending);
    step(2831);
    assert(p2.state == P2_FAILED && !ui.retrieval_pending && !p2.received_log_valid);
    assert(count_message("[P2] Receive timeout: no transfer progress") == 1);
    assert(count_message("[UI] Data retrieval failed") == 1);
    assert(count_message("[UI] Data retrieval complete") == 0);
    step(2931); step(3031); step(3131); step(3231); step(3331);
    assert(!levels[6] && led_rises == 4); // CONFIG plus three failure flashes.
    drop_data = false;
    press(21, 3400); step(3431);
    assert(p2.received_log_valid && requests == 2);
    assert(mock_logger_matches(receive_buffer, p2.received_log_length));
    assert(count_message("[UI] Data retrieval complete") == 1);
}

static void test_clear_and_empty_retrieval(void)
{
    init(); press(22, 0);
    assert(sent_count == 0 && !ui.clear_pending && !levels[6]);
    assert(count_message("[UI] Configure first using GP20") == 1);
    configured();
    uint8_t original[100];
    assert(mock_logger_length() == sizeof(original));
    memcpy(original, mock_logger_data(), sizeof(original));
    press(21, 300); step(331); release(21, 340);
    step(481); step(581); step(731);
    assert(data_frames == 13 && clears == 0 && p2.received_log_length == 100);
    // Debounce a bouncing GP22 press before allowing CLEAR_LOG.
    levels[22] = false; step(800); levels[22] = true; step(810);
    levels[22] = false; step(820); step(849); assert(clears == 0 && !ui.clear_pending);
    step(850); assert(ui.clear_pending && p2.state == P2_WAIT_CLEAR_ACK);
    assert(!pico2_clear_log(&p2));
    step(851);
    assert(clears == 1 && !ui.clear_pending && p2.state == P2_COMPLETE);
    assert(count_message("[P2] CLEAR_LOG acknowledged") == 1);
    assert(count_message("[UI] Log cleared") == 1 && ui.configured);
    assert(p1.logger_data_length == 0 && p1.total_blocks == 0);
    assert(p1.logger_data == mock_logger_data());
    assert(memcmp(original, mock_logger_data(), sizeof(original)) == 0);
    step(1350); assert(levels[6]); step(1351); assert(!levels[6]);
    step(4000); assert(clears == 1); // Held GP22 does not repeat.
    press(21, 4100); step(4131);
    assert(p2.received_log_length == 0 && p2.expected_block == 0 && data_frames == 13);
    assert(p2.state == P2_COMPLETE && !ui.retrieval_pending && requests == 2);
    assert(count_message("[P2] TRANSFER_END validated") == 2);
    assert(count_message("[P2] Retrieved log is empty (0 bytes)") == 1);
    assert(count_message("[P2] Retrieved compressed log matches Pico 1 data") == 1);
    assert(count_message("[UI] Data retrieval complete") == 2);
    // Empty validation must reject a stale full-length result too.
    assert(!validate_log(original, sizeof(original)));
    step(4281); step(4381); step(4531);
    // Release bounce cannot re-arm CLEAR_LOG.
    levels[22] = true; step(4540); levels[22] = false; step(4550); step(4580);
    assert(clears == 1);
    release(22, 4600); press(22, 4700); step(4731);
    assert(clears == 2 && p1.logger_data_length == 0 && !ui.clear_pending);
    assert(count_message("[UI] Log cleared") == 2);
    assert(memcmp(original, mock_logger_data(), sizeof(original)) == 0);
}
static void test_clear_retry_busy_and_failure(void)
{
    configured(); drops_remaining = 1;
    press(22, 300); step(331);
    assert(ui.clear_pending && p1.logger_data_length == 0);
    press(20, 340); press(21, 380); release(22, 420); press(22, 460);
    assert(clears == 1 && requests == 0 && count_message("[UI] Busy;") == 3);
    step(829); assert(p2.pending_command.retries == 0);
    step(830); step(831);
    assert(clears == 2 && !ui.clear_pending && p2.pending_command.retries == 1);
    assert(count_message("[UI] Log cleared") == 1);
    step(1331); step(5000); assert(clears == 2 && requests == 0);

    configured(); drops_remaining = 100;
    press(22, 300); step(331);
    step(830); step(1330); step(1830); step(2330);
    assert(clears == 4 && p2.pending_command.retries == MAX_RETRIES);
    assert(p2.state == P2_FAILED && !ui.clear_pending);
    assert(count_message("[UI] CLEAR_LOG failed") == 1);
    assert(count_message("[UI] Log cleared") == 0);
    step(2430); assert(!levels[6]); step(2530); assert(levels[6]);
    step(2630); step(2730); step(2830);
    assert(!levels[6] && led_rises == 4);
    // Missing ACK means unknown outcome to the UI; the endpoint may have cleared.
    assert(p1.logger_data_length == 0);
    drops_remaining = 0; release(22, 2900); press(22, 3000); step(3031);
    assert(clears == 5 && count_message("[UI] Log cleared") == 1);
}

int main(void)
{
    test_clear_and_empty_retrieval();
    test_clear_retry_busy_and_failure();
    test_retrieval();
    test_retrieval_busy_and_failure();
    test_remote_receive_timeout();
    test_gp21_during_config();
    test_press_and_success();
    test_retry_and_busy_press();
    test_failure_and_recovery();
    test_ack_validation_and_nack();
    puts("PASS UI integration: 100-byte/13-frame repeated retrieval, explicit CLEAR_LOG/empty retrieval, source preservation, CONFIG exchange, debounce/hold, ACK/NACK validation, retries, LED patterns, failure recovery");
    return 0;
}
