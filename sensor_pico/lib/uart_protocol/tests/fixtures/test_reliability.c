// Deterministic host tests: real endpoints, real encoded bytes, simulated time.
// These deliberately inject faults that the five unchanged golden modes omit.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico1/pico1_protocol.h"
#include "pico2/pico2_protocol.h"
#include "protocol/stream_parser.h"

// Fail immediately on Windows as well, without opening a runtime crash dialog.
#undef assert
#define assert(condition) do { if (!(condition)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
    exit(EXIT_FAILURE); } } while (0)

#define SESSION 0x11223344u
#define RECEIVE_DEADLINE_MS ((MAX_RETRIES + 2u) * ACK_TIMEOUT_MS)
static uart_inst_t sensor_uart = {1}, receiver_uart = {2};
static pico1_ctx_t p1;
static pico2_ctx_t p2;
static uint8_t source[100], destination[128];
static uint64_t clock_ms;
static unsigned completions;
static bool clear_on_complete;
static bool sensor_ticks_first;
static stream_parser_t sensor_parser, receiver_parser;
static struct packet {
    unsigned sender;
    protocol_frame_t frame;
    uint8_t bytes[MAX_STUFFED_FRAME_SIZE];
    size_t length;
} packets[2048];
static size_t written, delivered;
// -1 means every matching packet; a positive count drops that many copies.
static int drop_request_ack, drop_data_ack, drop_end_ack;
static int corrupt_data, truncate_data, drop_data, drop_end;
static uint16_t request_sequence;

uint64_t time_us_64(void) { return clock_ms * 1000u; }

void uart_write_blocking(uart_inst_t *uart, const uint8_t *bytes, size_t length)
{
    assert(written < sizeof(packets) / sizeof(packets[0]));
    struct packet *packet = &packets[written++];
    packet->sender = uart->id;
    packet->length = length;
    assert(length <= sizeof(packet->bytes));
    memcpy(packet->bytes, bytes, length);
    assert(frame_decode(bytes, length, &packet->frame));
}

static void completed(pico2_ctx_t *receiver)
{
    completions++;
    assert(receiver->received_log_valid);
    assert(receiver->received_log_length == p1.logger_data_length);
    assert(memcmp(receiver->received_log, source, receiver->received_log_length) == 0);
    if (clear_on_complete) { assert(pico2_clear_log(receiver)); }
}

static void setup(size_t length, size_t capacity)
{
    clock_ms = 0;
    written = delivered = completions = 0;
    clear_on_complete = false;
    sensor_ticks_first = false;
    drop_request_ack = drop_data_ack = drop_end_ack = 0;
    corrupt_data = truncate_data = drop_data = drop_end = 0;
    for (size_t i = 0; i < sizeof(source); i++) { source[i] = (uint8_t)(i + 0x70); }
    memset(destination, 0xa5, sizeof(destination));
    assert(pico1_init(&p1, (protocol_link_t){&sensor_uart, SESSION, NULL},
                     source, length, (pico1_hooks_t){0}));
    assert(pico2_init(&p2, (protocol_link_t){&receiver_uart, SESSION, NULL},
                     destination, capacity, 1700000000u, 3600u,
                     (pico2_hooks_t){.transfer_complete = completed}));
    // Application CONFIG has completed; no demo HELLO auto-chain is needed.
    p2.state = P2_COMPLETE;
    stream_parser_init(&sensor_parser);
    stream_parser_init(&receiver_parser);
}

static bool consume_fault(int *remaining)
{
    if (*remaining == 0) { return false; }
    if (*remaining > 0) { --*remaining; }
    return true;
}

static uint8_t acked_type(uint16_t sequence, size_t before)
{
    for (size_t i = before; i > 0; i--)
    {
        if (packets[i - 1].sender == sensor_uart.id &&
            packets[i - 1].frame.sequence == sequence)
        {
            return packets[i - 1].frame.message_type;
        }
    }
    return 0;
}

static void pump(void)
{
    while (delivered < written)
    {
        size_t index = delivered++;
        struct packet packet = packets[index];
        bool from_sensor = packet.sender == sensor_uart.id;
        uint8_t type = packet.frame.message_type;
        if (from_sensor && type == MSG_ACK &&
            read_u16_le(packet.frame.payload) == request_sequence &&
            consume_fault(&drop_request_ack)) { continue; }
        if (!from_sensor && type == MSG_ACK)
        {
            uint8_t ack_type = acked_type(read_u16_le(packet.frame.payload), index);
            if (ack_type == MSG_DATA && consume_fault(&drop_data_ack)) { continue; }
            if (ack_type == MSG_TRANSFER_END && consume_fault(&drop_end_ack)) { continue; }
        }
        if (from_sensor && type == MSG_DATA)
        {
            if (consume_fault(&drop_data)) { continue; }
            if (consume_fault(&corrupt_data)) { packet.bytes[1] ^= 0x40; }
            if (consume_fault(&truncate_data)) { packet.length /= 2; }
        }
        if (from_sensor && type == MSG_TRANSFER_END && consume_fault(&drop_end)) { continue; }
        stream_parser_t *parser = from_sensor ? &receiver_parser : &sensor_parser;
        for (size_t i = 0; i < packet.length; i++)
        {
            protocol_frame_t frame;
            if (stream_parser_feed(parser, packet.bytes[i]) &&
                frame_decode(parser->buffer, parser->length, &frame))
            {
                if (from_sensor) { pico2_handle_frame(&p2, &frame); }
                else { pico1_handle_frame(&p1, &frame); }
            }
        }
    }
}

static void start(void)
{
    assert(pico2_request_data(&p2));
    assert(!p2.received_log_valid && p2.received_log_length == 0);
    request_sequence = p2.pending_command.sequence;
}

static void advance(unsigned milliseconds)
{
    for (unsigned i = 0; i < milliseconds; i += 50)
    {
        clock_ms += 50;
        if (sensor_ticks_first) { pico1_tick(&p1); pico2_tick(&p2); }
        else { pico2_tick(&p2); pico1_tick(&p1); }
        pump();
    }
}

static protocol_frame_t frame(uint8_t type, uint16_t sequence, uint16_t value)
{
    protocol_frame_t result = {0};
    result.version = PROTOCOL_VERSION;
    result.session_id = SESSION;
    result.message_type = type;
    result.sequence = sequence;
    result.payload_length = 2;
    write_u16_le(result.payload, value);
    if (type == MSG_DATA)
    {
        result.payload_length += DATA_CHUNK_SIZE;
        memcpy(result.payload + DATA_BLOCK_INDEX_SIZE, source, DATA_CHUNK_SIZE);
    }
    return result;
}

static void begin_receive(void)
{
    start();
    protocol_frame_t ack = frame(MSG_ACK, 100, request_sequence);
    pico2_handle_frame(&p2, &ack);
    assert(p2.state == P2_RECEIVING_DATA);
    delivered = written; // Direct-frame tests do not run the other endpoint.
}

static void test_receive_timeout(void)
{
    setup(100, sizeof(destination));
    begin_receive();
    clock_ms = RECEIVE_DEADLINE_MS - 1;
    pico2_tick(&p2);
    assert(p2.state == P2_RECEIVING_DATA);
    clock_ms++;
    pico2_tick(&p2);
    assert(p2.state == P2_FAILED && !p2.pending_command.active && completions == 0);
    assert(!p2.received_log_valid);

    // Only accepting a new block extends the deadline. Duplicate traffic cannot
    // keep an interrupted retrieval alive indefinitely.
    setup(100, sizeof(destination));
    begin_receive();
    clock_ms = 400;
    protocol_frame_t data = frame(MSG_DATA, 101, 0);
    pico2_handle_frame(&p2, &data);
    clock_ms = 400 + RECEIVE_DEADLINE_MS - 1;
    pico2_handle_frame(&p2, &data);
    pico2_tick(&p2);
    assert(p2.state == P2_RECEIVING_DATA);
    clock_ms++;
    pico2_tick(&p2);
    assert(p2.state == P2_FAILED && p2.received_log_length == 0 && completions == 0);
    size_t count = written;
    pico2_handle_frame(&p2, &data);
    protocol_frame_t end = frame(MSG_TRANSFER_END, 102, 1);
    pico2_handle_frame(&p2, &end);
    assert(written == count && p2.state == P2_FAILED && completions == 0);

    // A late complete frame processed before the loop's next tick still expires.
    setup(0, sizeof(destination));
    begin_receive();
    clock_ms = RECEIVE_DEADLINE_MS;
    end = frame(MSG_TRANSFER_END, 101, 0);
    pico2_handle_frame(&p2, &end);
    assert(p2.state == P2_FAILED && completions == 0 && !p2.received_log_valid);
}

static void test_state_guard(void)
{
    const pico2_state_t states[] = {P2_WAIT_HELLO_ACK, P2_WAIT_CONFIG_ACK,
        P2_WAIT_CONFIG_ONLY_ACK, P2_WAIT_REQUEST_ACK, P2_WAIT_CLEAR_ACK,
        P2_COMPLETE, P2_FAILED};
    for (size_t i = 0; i < sizeof(states) / sizeof(states[0]); i++)
    {
        setup(0, sizeof(destination));
        p2.state = states[i];
        protocol_frame_t end = frame(MSG_TRANSFER_END, 100, 0);
        pico2_handle_frame(&p2, &end);
        protocol_frame_t data = frame(MSG_DATA, 101, 0);
        pico2_handle_frame(&p2, &data);
        assert(p2.state == states[i] && p2.received_log_length == 0);
        assert(written == 0 && completions == 0);
    }
    setup(100, sizeof(destination));
    begin_receive();
    assert(!pico2_send_config(&p2)); // No command may replace an active transfer.
    assert(!pico2_send_command(&p2, MSG_CLEAR_LOG, NULL, 0, P2_WAIT_CLEAR_ACK));
}

static void test_end_duplicate(void)
{
    for (size_t length = 0; length <= 100; length += 100)
    {
        setup(length, sizeof(destination));
        drop_end_ack = 1;
        start();
        pump();
        assert(completions == 1 && p1.pending_transfer.active);
        advance(ACK_TIMEOUT_MS);
        assert(completions == 1 && !p1.pending_transfer.active);
        assert(p2.state == P2_COMPLETE);
        // Delayed END during a fresh request must not finish that new request.
        protocol_frame_t old_end = {0};
        for (size_t i = 0; i < written; i++)
        {
            if (packets[i].frame.message_type == MSG_TRANSFER_END) { old_end = packets[i].frame; }
        }
        start();
        pico2_handle_frame(&p2, &old_end);
        assert(p2.state == P2_WAIT_REQUEST_ACK && p2.pending_command.active && completions == 1);
        pump();
        assert(completions == 2 && p2.state == P2_COMPLETE);
    }
    // A completion hook can start CLEAR_LOG; duplicate END must not alter it.
    setup(100, sizeof(destination));
    clear_on_complete = true;
    start();
    pump();
    protocol_frame_t end = {0};
    for (size_t i = 0; i < written; i++)
    {
        if (packets[i].frame.message_type == MSG_TRANSFER_END) { end = packets[i].frame; }
    }
    p2.state = P2_WAIT_CLEAR_ACK;
    pico2_handle_frame(&p2, &end);
    assert(completions == 1 && p2.state == P2_WAIT_CLEAR_ACK);

    // An old END after a new DATA block must not validate a partial new log.
    setup(8, sizeof(destination));
    start();
    pump();
    end = frame(MSG_TRANSFER_END, p2.completed_end_sequence, 1);
    p1.logger_data_length = 100;
    begin_receive();
    protocol_frame_t data = frame(MSG_DATA, 200, 0);
    pico2_handle_frame(&p2, &data);
    pico2_handle_frame(&p2, &end);
    assert(completions == 1 && p2.state == P2_RECEIVING_DATA && !p2.received_log_valid);
}

static void test_changed_duplicate(void)
{
    setup(100, sizeof(destination));
    begin_receive();
    protocol_frame_t data = frame(MSG_DATA, 101, 0);
    pico2_handle_frame(&p2, &data);
    size_t count = written;
    data.payload[DATA_BLOCK_INDEX_SIZE] ^= 1;
    pico2_handle_frame(&p2, &data);
    assert(packets[count].frame.message_type == MSG_NACK);
    data.payload[DATA_BLOCK_INDEX_SIZE] ^= 1;
    data.sequence++;
    pico2_handle_frame(&p2, &data);
    assert(packets[count + 1].frame.message_type == MSG_NACK);
    assert(p2.received_log_length == DATA_CHUNK_SIZE);
    assert(memcmp(destination, source, DATA_CHUNK_SIZE) == 0);
}

static void test_overflow_recovery(void)
{
    setup(100, 9);
    start();
    pump();
    assert(p2.state == P2_FAILED && p2.received_log_length == 0 && completions == 0);
    assert(destination[9] == 0xa5);
    advance(RECEIVE_DEADLINE_MS);
    assert(!p1.pending_transfer.active);
    // A fresh application request resets transfer metadata, with the same RAM.
    p1.logger_data_length = 8;
    start();
    pump();
    assert(p2.state == P2_COMPLETE && completions == 1 && p2.received_log_length == 8);

    // Block counters must not wrap and make an incomplete log appear empty.
    setup(100, sizeof(destination));
    begin_receive();
    p2.expected_block = UINT16_MAX;
    protocol_frame_t data = frame(MSG_DATA, 101, UINT16_MAX);
    pico2_handle_frame(&p2, &data);
    assert(p2.state == P2_FAILED && !p2.received_log_valid && completions == 0);
}

static void test_pending_failure(void)
{
    setup(100, sizeof(destination));
    start();
    // No peer: initial command plus exactly three retransmissions, then stop.
    for (unsigned i = 0; i <= MAX_RETRIES; i++)
    {
        clock_ms += ACK_TIMEOUT_MS;
        pico2_tick(&p2);
    }
    assert(p2.state == P2_FAILED && !p2.pending_command.active);
    assert(written == MAX_RETRIES + 1);
    size_t count = written;
    protocol_frame_t late_ack = frame(MSG_ACK, 100, request_sequence);
    pico2_handle_frame(&p2, &late_ack);
    pico2_tick(&p2);
    assert(p2.state == P2_FAILED && written == count);
    // Exhausting the same budget through NACKs must also deactivate tracking.
    setup(100, sizeof(destination));
    start();
    protocol_frame_t nack = frame(MSG_NACK, 100, request_sequence);
    nack.payload_length = NACK_PAYLOAD_SIZE;
    nack.payload[2] = NACK_BAD_PAYLOAD;
    for (unsigned i = 0; i <= MAX_RETRIES; i++) { pico2_handle_frame(&p2, &nack); }
    assert(p2.state == P2_FAILED && !p2.pending_command.active);
}

static unsigned parse_bytes(stream_parser_t *parser, const uint8_t *bytes, size_t length)
{
    unsigned valid = 0;
    for (size_t i = 0; i < length; i++)
    {
        protocol_frame_t result;
        if (stream_parser_feed(parser, bytes[i]) &&
            frame_decode(parser->buffer, parser->length, &result)) { valid++; }
    }
    return valid;
}

static void test_parser_resync(void)
{
    uint8_t bytes[MAX_STUFFED_FRAME_SIZE];
    size_t length = frame_encode(MSG_HELLO, 0, SESSION, 10, NULL, 0, bytes, sizeof(bytes));
    for (unsigned fault = 0; fault < 4; fault++)
    {
        stream_parser_t parser;
        stream_parser_init(&parser);
        if (fault == 0) { parse_bytes(&parser, bytes, length / 2); }
        if (fault == 1) { parse_bytes(&parser, bytes + 1, length - 1); }
        if (fault == 2) { parse_bytes(&parser, bytes, length - 1); }
        if (fault == 3)
        {
            stream_parser_feed(&parser, SOF);
            for (size_t i = 0; i < MAX_STUFFED_FRAME_SIZE * 2; i++) { stream_parser_feed(&parser, 0); }
        }
        // Recover within two intact copies, comfortably inside three retries.
        unsigned valid = parse_bytes(&parser, bytes, length);
        valid += parse_bytes(&parser, bytes, length);
        assert(valid > 0);
    }
}

static void test_wire_faults(void)
{
    for (unsigned fault = 0; fault < 11; fault++)
    {
        setup(fault == 7 ? 0 : 100, sizeof(destination));
        if (fault == 0 || fault == 7) { drop_request_ack = 1; }
        if (fault == 1) { drop_data_ack = 1; }
        if (fault == 2) { corrupt_data = 1; }
        if (fault == 3) { truncate_data = 1; }
        if (fault == 4) { drop_data = -1; }
        if (fault == 5) { drop_end = -1; }
        if (fault == 6) { drop_data_ack = -1; }
        if (fault == 8) { corrupt_data = -1; }
        if (fault == 9) { drop_end_ack = -1; }
        if (fault == 10) { drop_request_ack = 1; sensor_ticks_first = true; }
        start();
        pump();
        advance(5000);
        assert(!p1.pending_transfer.active && !p2.pending_command.active);
        if ((fault >= 4 && fault <= 6) || fault == 8)
        {
            assert(p2.state == P2_FAILED && completions == 0 && p2.received_log_length == 0);
            assert(!p2.received_log_valid);
            // Restoring the wire allows a fresh retrieval without rebooting.
            drop_data = drop_end = drop_data_ack = corrupt_data = 0;
            start();
            pump();
            assert(p2.state == P2_COMPLETE && completions == 1);
        }
        else { assert(p2.state == P2_COMPLETE && completions == 1); }
        assert(p2.received_log_valid);
    }
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    struct { const char *name; void (*run)(void); } cases[] = {
        {"receive_timeout", test_receive_timeout}, {"state_guard", test_state_guard},
        {"end_duplicate", test_end_duplicate}, {"changed_duplicate", test_changed_duplicate},
        {"overflow_recovery", test_overflow_recovery}, {"pending_failure", test_pending_failure},
        {"parser_resync", test_parser_resync}, {"wire_faults", test_wire_faults},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        if (strcmp(argv[1], cases[i].name) == 0)
        {
            cases[i].run();
            printf("PASS %s\n", cases[i].name);
            return 0;
        }
    }
    return EXIT_FAILURE;
}
