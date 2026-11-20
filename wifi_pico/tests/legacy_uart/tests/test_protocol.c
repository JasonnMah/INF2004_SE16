// Host checks supplement, rather than replace, the actual RP2040 regressions.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "protocol/crc16.h"
#include "protocol/stream_parser.h"
#include "pico1/pico1_protocol.h"
#include "pico2/pico2_protocol.h"

#define SESSION 0x11223344u
static uart_inst_t sensor_uart = {1};
static uart_inst_t receiver_uart = {2};
static uint64_t clock_ms;
static struct {
    unsigned sender;
    protocol_frame_t frame;
} transmissions[256];
static size_t tx_count;

uint64_t time_us_64(void) { return clock_ms * 1000; }

void uart_write_blocking(uart_inst_t *uart, const uint8_t *data, size_t length)
{
    assert(tx_count < sizeof(transmissions) / sizeof(transmissions[0]));
    transmissions[tx_count].sender = uart->id;
    assert(frame_decode(data, length, &transmissions[tx_count].frame));
    tx_count++;
}

static protocol_frame_t command(uint8_t type, uint16_t sequence)
{
    protocol_frame_t frame = {0};
    frame.version = PROTOCOL_VERSION;
    frame.session_id = SESSION;
    frame.message_type = type;
    frame.sequence = sequence;
    return frame;
}

static void init(pico1_ctx_t *p1, pico2_ctx_t *p2,
                 const uint8_t *source, size_t length, uint8_t *dest, size_t capacity)
{
    tx_count = 0;
    clock_ms = 0;
    assert(pico1_init(p1, (protocol_link_t){&sensor_uart, SESSION, NULL},
                      source, length, (pico1_hooks_t){0}));
    assert(pico2_init(p2, (protocol_link_t){&receiver_uart, SESSION, NULL},
                      dest, capacity, 1700000000u, 3600u, (pico2_hooks_t){0}));
}

static void test_frames(void)
{
    assert(crc16_x25((const uint8_t *)"123456789", 9) == 0x906e);
    assert(crc16_x25(NULL, 0) == 0);
    uint8_t payload[MAX_PAYLOAD_SIZE];
    for (size_t i = 0; i < sizeof(payload); i++) { payload[i] = (uint8_t)i; }
    uint8_t encoded[MAX_STUFFED_FRAME_SIZE];
    size_t length = frame_encode(MSG_DATA, 0x7d, SESSION, 0x7e7d,
                                 payload, sizeof(payload), encoded, sizeof(encoded));
    assert(length > 0);
    protocol_frame_t frame;
    assert(frame_decode(encoded, length, &frame));
    assert(frame.flags == 0x7d && frame.sequence == 0x7e7d);
    assert(frame.session_id == SESSION && frame.payload_length == sizeof(payload));
    assert(memcmp(frame.payload, payload, sizeof(payload)) == 0);
    assert(frame_encode(MSG_DATA, 0, SESSION, 1, payload, MAX_PAYLOAD_SIZE + 1,
                         encoded, sizeof(encoded)) == 0);
    assert(frame_encode(MSG_DATA, 0, SESSION, 1, NULL, 1, encoded, sizeof(encoded)) == 0);
    assert(frame_encode(MSG_DATA, 0, SESSION, 1, payload, sizeof(payload), encoded, 2) == 0);
    length = frame_encode(MSG_DATA, 0, SESSION, 1, payload, sizeof(payload), encoded, sizeof(encoded));
    encoded[FRAME_SESSION_OFFSET] ^= 1;
    assert(!frame_decode(encoded, length, &frame));
    uint8_t malformed[] = {SOF, ESCAPE, 0, SOF};
    assert(unstuff_frame(malformed, sizeof(malformed), encoded, sizeof(encoded)) == 0);
    uint8_t dangling[] = {SOF, ESCAPE, SOF};
    assert(unstuff_frame(dangling, sizeof(dangling), encoded, sizeof(encoded)) == 0);

    stream_parser_t parser;
    stream_parser_init(&parser);
    assert(!stream_parser_feed(&parser, 'x'));
    assert(!stream_parser_feed(&parser, SOF));
    for (size_t i = 0; i < MAX_STUFFED_FRAME_SIZE; i++)
    {
        assert(!stream_parser_feed(&parser, 0));
    }
    assert(!parser.receiving);
    length = frame_encode(MSG_HELLO, 0, SESSION, 1, NULL, 0, encoded, sizeof(encoded));
    for (size_t i = 0; i < length; i++)
    {
        assert(stream_parser_feed(&parser, encoded[i]) == (i == length - 1));
    }
    assert(frame_decode(parser.buffer, parser.length, &frame));
}

static void test_buffer_transfer(size_t length)
{
    // Larger than one frame, and deliberately not a multiple of DATA_CHUNK_SIZE.
    uint8_t source[513], dest[513];
    for (size_t i = 0; i < sizeof(source); i++) { source[i] = (uint8_t)i; }
    pico1_ctx_t p1;
    pico2_ctx_t p2;
    init(&p1, &p2, source, length, dest, sizeof(dest));
    assert(p1.logger_data == source); // The source is borrowed, not copied.
    assert(pico2_send_command(&p2, MSG_HELLO, NULL, 0, P2_WAIT_HELLO_ACK));
    for (size_t delivered = 0; delivered < tx_count; delivered++)
    {
        if (transmissions[delivered].sender == sensor_uart.id)
        {
            pico2_handle_frame(&p2, &transmissions[delivered].frame);
        }
        else
        {
            pico1_handle_frame(&p1, &transmissions[delivered].frame);
        }
    }
    assert(p2.state == P2_COMPLETE);
    assert(p2.received_log_length == length && memcmp(source, dest, length) == 0);
    assert(p2.expected_block == (length + DATA_CHUNK_SIZE - 1) / DATA_CHUNK_SIZE);
    assert(p1.transfer_state == P1_TRANSFER_IDLE && !p1.pending_transfer.active);
    assert(p1.configured && p1.current_timestamp == 1700000000u && p1.sampling_interval == 3600u);
    // No demo callback: finishing a transfer does not automatically clear source data.
    assert(p1.logger_data_length == length);
    protocol_frame_t clear = command(MSG_CLEAR_LOG, 100);
    pico1_handle_frame(&p1, &clear);
    assert(p1.logger_data_length == 0);
    size_t count = tx_count;
    pico1_handle_frame(&p1, &clear);
    assert(tx_count == count + 1 && transmissions[count].frame.message_type == MSG_ACK);
    for (size_t i = 0; i < sizeof(source); i++) { assert(source[i] == (uint8_t)i); }
}

static void test_commands_and_retries(void)
{
    uint8_t source[8] = {0}, dest[16];
    pico1_ctx_t p1;
    pico2_ctx_t p2;
    init(&p1, &p2, source, sizeof(source), dest, sizeof(dest));
    protocol_frame_t config = command(MSG_CONFIG, 1);
    config.payload_length = CONFIG_PAYLOAD_SIZE;
    write_u32_le(config.payload, 10);
    write_u32_le(&config.payload[4], 20);
    pico1_handle_frame(&p1, &config);
    write_u32_le(config.payload, 99);
    pico1_handle_frame(&p1, &config);
    assert(p1.current_timestamp == 10 && tx_count == 2); // Duplicate only ACKs.
    protocol_frame_t request = command(MSG_REQUEST_DATA, 2);
    request.payload_length = REQUEST_DATA_PAYLOAD_SIZE;
    write_u16_le(request.payload, 2);
    pico1_handle_frame(&p1, &request);
    assert(transmissions[2].frame.message_type == MSG_NACK);
    assert(transmissions[2].frame.payload[2] == NACK_BAD_BLOCK);
    request.session_id++;
    pico1_handle_frame(&p1, &request);
    assert(tx_count == 3);
    request = command(MSG_CONFIG, 3); // Malformed CONFIG must remain rejected.
    pico1_handle_frame(&p1, &request);
    assert(transmissions[3].frame.payload[2] == NACK_BAD_PAYLOAD);

    tx_count = 0;
    assert(pico2_send_command(&p2, MSG_HELLO, NULL, 0, P2_WAIT_HELLO_ACK));
    clock_ms = 499;
    pico2_tick(&p2);
    assert(tx_count == 1);
    for (unsigned retry = 1; retry <= MAX_RETRIES; retry++)
    {
        clock_ms = retry * ACK_TIMEOUT_MS;
        pico2_tick(&p2);
        assert(tx_count == 1 + retry);
        assert(transmissions[retry].frame.sequence == transmissions[0].frame.sequence);
    }
    clock_ms += ACK_TIMEOUT_MS;
    pico2_tick(&p2);
    assert(p2.state == P2_FAILED && tx_count == 4);

    init(&p1, &p2, source, sizeof(source), dest, sizeof(dest));
    request = command(MSG_REQUEST_DATA, 1);
    request.payload_length = REQUEST_DATA_PAYLOAD_SIZE;
    pico1_handle_frame(&p1, &request);
    assert(tx_count == 2 && p1.pending_transfer.active);
    for (unsigned retry = 1; retry <= MAX_RETRIES; retry++)
    {
        clock_ms = retry * ACK_TIMEOUT_MS;
        pico1_tick(&p1);
        assert(tx_count == 2 + retry);
    }
    clock_ms += ACK_TIMEOUT_MS;
    pico1_tick(&p1);
    assert(!p1.pending_transfer.active && p1.transfer_state == P1_TRANSFER_IDLE);
}

static bool reject_log(const uint8_t *data, size_t length)
{
    (void)data;
    (void)length;
    return false;
}

static void begin_receive(pico2_ctx_t *p2)
{
    p2->state = P2_COMPLETE; // Application configuration is already complete.
    assert(pico2_request_data(p2));
    protocol_frame_t ack = command(MSG_ACK, 100);
    ack.payload_length = ACK_PAYLOAD_SIZE;
    write_u16_le(ack.payload, p2->pending_command.sequence);
    pico2_handle_frame(p2, &ack);
    assert(p2->state == P2_RECEIVING_DATA);
    tx_count = 0;
}

static void test_receiver_rejections(void)
{
    uint8_t source[8] = {0}, dest[3] = {0};
    pico1_ctx_t p1;
    pico2_ctx_t p2;
    init(&p1, &p2, source, sizeof(source), dest, sizeof(dest));
    begin_receive(&p2);
    protocol_frame_t data = command(MSG_DATA, 1);
    data.payload_length = 4;
    data.payload[2] = 0x7e;
    data.payload[3] = 0x7d;
    pico2_handle_frame(&p2, &data);
    pico2_handle_frame(&p2, &data);
    assert(p2.received_log_length == 2 && p2.expected_block == 1);
    assert(tx_count == 2 && transmissions[1].frame.message_type == MSG_ACK);
    write_u16_le(data.payload, 2);
    pico2_handle_frame(&p2, &data);
    assert(transmissions[2].frame.payload[2] == NACK_BAD_BLOCK);
    write_u16_le(data.payload, 1);
    pico2_handle_frame(&p2, &data); // Capacity exhaustion cannot append a partial block.
    assert(transmissions[3].frame.payload[2] == NACK_BAD_PAYLOAD);
    assert(p2.state == P2_FAILED && !p2.received_log_valid);
    assert(p2.received_log_length == 0 && p2.expected_block == 0 && dest[2] == 0);

    begin_receive(&p2);
    write_u16_le(data.payload, 0);
    pico2_handle_frame(&p2, &data);
    protocol_frame_t end = command(MSG_TRANSFER_END, 2);
    end.payload_length = TRANSFER_END_PAYLOAD_SIZE;
    write_u16_le(end.payload, 2);
    pico2_handle_frame(&p2, &end);
    assert(p2.state == P2_FAILED && transmissions[1].frame.payload[2] == NACK_BAD_BLOCK);
    begin_receive(&p2);
    pico2_handle_frame(&p2, &data);
    p2.hooks.validate_log = reject_log;
    write_u16_le(end.payload, 1);
    pico2_handle_frame(&p2, &end);
    assert(p2.state == P2_FAILED && transmissions[1].frame.payload[2] == NACK_BAD_BLOCK);
    assert(!pico1_init(&p1, p1.link, NULL, 1, (pico1_hooks_t){0}));
    assert(!pico1_init(&p1, p1.link, source,
                       (size_t)UINT16_MAX * DATA_CHUNK_SIZE + 1, (pico1_hooks_t){0}));
}

int main(void)
{
    test_frames();
    test_buffer_transfer(513);
    test_buffer_transfer(0);
    test_commands_and_retries();
    test_receiver_rejections();
    puts("PASS host protocol checks: CRC/framing, 513-byte/empty transfer, duplicates, rejection, timeout/retry limits");
    return 0;
}
