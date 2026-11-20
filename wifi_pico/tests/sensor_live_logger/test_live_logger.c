/* Host tests of the real RAM adapter and shared UART endpoints, not hardware. */
/* Pretend UART wires and a controllable clock connect both real protocol
 * endpoints. Dropping selected ACKs lets us check recovery without a Pico. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "live_logger.h"
#include "pico1/pico1_protocol.h"
#include "pico2/pico2_protocol.h"
#include "protocol/stream_parser.h"

static live_logger_t log_data;
static pico1_ctx_t p1;
static pico2_ctx_t p2;
static stream_parser_t parser[2];
/* Side 0 receives as the Wi-Fi Pico; side 1 receives as the Sensor Pico. */
static uint8_t wire[2][8192], received[LIVE_LOG_CAPACITY];
static unsigned head[2], tail[2], sides[2] = {0, 1};
static uint64_t clock_now;
/* A lease means the Sensor protocol is borrowing one fixed snapshot. Count
 * releases/completions to catch leaks or duplicate completion notifications. */
static unsigned completions, leases, releases, configurations, end_acks;
static bool drop_first_end, drop_data_acks;

static uint64_t clock_ms(void *context) { (void)context; return clock_now; }
static bool configure(void *context, uint32_t timestamp, uint32_t seconds)
{
    ++configurations;
    return live_logger_configure(context, timestamp, seconds);
}
static bool snapshot(void *context, const uint8_t **bytes, size_t *length)
{
    bool ok = live_logger_snapshot(context, bytes, length);
    if (ok) ++leases;
    return ok;
}
static void release(void *context, bool completed)
{ (void)completed; ++releases; live_logger_release(context); }
static pico1_storage_result_t clear(void *context)
{ return live_logger_clear(context) ? P1_STORAGE_READY : P1_STORAGE_ERROR; }
/* Connect the same RAM functions used by main.c to the real Sensor endpoint. */
static const pico1_backend_t backend = {
    .context = &log_data, .configure = configure, .snapshot = snapshot,
    .clear_begin = clear, .release = release
};
static void complete(pico2_ctx_t *endpoint)
{ assert(endpoint->received_log_valid); ++completions; }
/* Replace the hardware UART write. Normally deliver bytes to the other side;
 * selected tests discard ACKs to imitate loss on the physical connection. */
static void transmit(void *context, const uint8_t *bytes, size_t length)
{
    unsigned side = *(unsigned *)context;
    protocol_frame_t frame;
    assert(frame_decode(bytes, length, &frame));
    if (!side && frame.message_type == MSG_ACK) {
        if (p1.transfer_state == P1_WAIT_DATA_ACK && drop_data_acks) return;
        if (p1.transfer_state == P1_WAIT_END_ACK) {
            ++end_acks;
            if (drop_first_end) { drop_first_end = false; return; }
        }
    }
    for (size_t i = 0; i < length; ++i) {
        assert(head[1u-side] - tail[1u-side] < sizeof(wire[0]));
        wire[1u-side][head[1u-side]++ % sizeof(wire[0])] = bytes[i];
    }
}
/* Deliver queued bytes and advance both retry timers in 10 ms steps. Simulated
 * time makes timeout tests repeatable without waiting that long in real time. */
static void pump(unsigned milliseconds)
{
    for (unsigned n = 0; n < milliseconds; n += 10) {
        for (unsigned side = 0; side < 2; ++side) {
            while (head[side] != tail[side]) {
                uint8_t byte = wire[side][tail[side]++ % sizeof(wire[0])];
                if (!stream_parser_feed(&parser[side], byte)) continue;
                protocol_frame_t frame;
                assert(frame_decode(parser[side].buffer, parser[side].length, &frame));
                if (!side) pico2_handle_frame(&p2, &frame);
                else pico1_handle_frame(&p1, &frame);
            }
        }
        pico1_tick(&p1);
        pico2_tick(&p2);
        clock_now += 10;
    }
}
/* Start each protocol scenario from a clean logger and a successful CONFIG. */
static void init_pair(void)
{
    live_logger_init(&log_data);
    memset(head, 0, sizeof(head)); memset(tail, 0, sizeof(tail));
    clock_now = 0; completions = leases = releases = configurations = end_acks = 0;
    drop_first_end = drop_data_acks = false;
    for (unsigned i = 0; i < 2; ++i) stream_parser_init(&parser[i]);
    protocol_link_t sensor_link = {.context = &sides[1], .session_id = 0x11223344,
                                  .write = transmit, .now_ms = clock_ms};
    protocol_link_t wifi_link = {.context = &sides[0], .session_id = 0x11223344,
                                .write = transmit, .now_ms = clock_ms};
    assert(pico1_init(&p1, sensor_link, NULL, 0, (pico1_hooks_t){.backend = &backend}));
    assert(pico2_init(&p2, wifi_link, received, sizeof(received), 1700000000, 3600,
                      (pico2_hooks_t){.transfer_complete = complete}));
    p2.state = P2_COMPLETE;
    assert(pico2_send_config(&p2)); pump(100);
    assert(p1.configured && log_data.running && p2.state == P2_COMPLETE);
}
/* Exercise interval limits, then write two sealed pages for run_tests.py to
 * decode: changing temperatures, one missing reading and repeated readings. */
static void save_fixture(const char *path)
{
    live_logger_init(&log_data);
    assert(live_logger_configure(&log_data, 1700000000, 4095u * 60u));
    assert(live_logger_append(&log_data, true, 3000));
    static live_logger_t before;
    before = log_data;
    /* Invalid CONFIG values must not partially change an existing valid log. */
    const uint32_t invalid[] = {0, 1, 59, 61, 4096u * 60u, UINT32_MAX};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        assert(!live_logger_configure(&log_data, 1700000000, invalid[i]));
        assert(!memcmp(&before, &log_data, sizeof(log_data)));
    }
    assert(live_logger_clear(&log_data));
    assert(live_logger_configure(&log_data, 1700000000, 60));
    assert(live_logger_clear(&log_data));
    assert(!live_logger_configure(&log_data, 1700000000, 1));
    assert(!live_logger_configure(&log_data, 1700000000, 0));
    assert(!live_logger_configure(&log_data, 1700000000, 60u * 65536u));
    assert(live_logger_configure(&log_data, 1700000000, 3600));
    assert(!live_logger_append(&log_data, false, 0)); /* No invented first reading. */
    for (int16_t t = 3000; t <= 3050; t += 25) assert(live_logger_append(&log_data, true, t));
    assert(live_logger_append(&log_data, true, 3100));
    assert(live_logger_append(&log_data, false, 0));
    for (unsigned i = 0; i < 600; ++i) assert(live_logger_append(&log_data, true, 3100));
    const uint8_t *bytes; size_t length;
    assert(live_logger_snapshot(&log_data, &bytes, &length));
    assert(length == 2 * ENC_PAGE_BYTES);
    FILE *out = fopen(path, "wb"); assert(out);
    assert(fwrite(bytes, 1, length, out) == length); assert(!fclose(out));
    live_logger_release(&log_data);
    assert(live_logger_clear(&log_data) && !log_data.running);
    assert(live_logger_snapshot(&log_data, &bytes, &length) && length == 0);
    live_logger_release(&log_data);
}
/* Assertions are the pass/fail checks. Any failed condition stops the test;
 * the final PASS message is reached only after all scenarios succeed. */
int main(int argc, char **argv)
{
    assert(argc == 2);
    save_fixture(argv[1]);
    /* Sealing the snapshot must neither change the encoder state nor allow a
     * later sample or clear to change bytes currently borrowed by UART. */
    live_logger_init(&log_data);
    assert(live_logger_configure(&log_data, 1700000000, 60));
    assert(live_logger_append(&log_data, true, -25));
    enc_t before = log_data.current;
    const uint8_t *bytes; size_t length;
    assert(live_logger_snapshot(&log_data, &bytes, &length));
    assert(!memcmp(&before, &log_data.current, sizeof(before)));
    uint8_t saved[LIVE_LOG_CAPACITY]; memcpy(saved, bytes, length);
    assert(live_logger_append(&log_data, true, 0));
    assert(!memcmp(saved, bytes, length));
    assert(!live_logger_snapshot(&log_data, &bytes, &length));
    assert(!live_logger_clear(&log_data));
    assert(!live_logger_configure(&log_data, 1, 60));
    live_logger_release(&log_data);
    assert(live_logger_clear(&log_data));
    /* Fill all four pages: retain every accepted sample and reject overflow. */
    assert(live_logger_configure(&log_data, 1700000000, 60));
    unsigned accepted = 0;
    while (live_logger_append(&log_data, true, 3000)) { assert(++accepted < 3000); }
    assert(accepted > 1500 && log_data.full && !log_data.running);
    assert(live_logger_snapshot(&log_data, &bytes, &length) && length == LIVE_LOG_CAPACITY);
    live_logger_release(&log_data);
    assert(live_logger_clear(&log_data));
    /* A duplicate CONFIG must not restart sampling/reconfigure the real adapter. */
    init_pair();
    protocol_frame_t command = {.message_type = MSG_CONFIG, .session_id = 0x11223344,
                                .sequence = p1.last_command_sequence, .payload_length = 8};
    write_u32_le(command.payload, 1700000000); write_u32_le(command.payload + 4, 3600);
    pico1_handle_frame(&p1, &command); pump(50); assert(configurations == 1);
    /* Invalid seconds leave the previous valid configuration intact. */
    p2.config_interval = 1; assert(pico2_send_config(&p2)); pump(100);
    assert(p2.state == P2_FAILED && log_data.interval_seconds == 3600);
    /* Lost final ACK: immutable snapshot, repeated END ACK, one completion. */
    init_pair(); assert(live_logger_append(&log_data, true, 3000));
    drop_first_end = true; assert(pico2_request_data(&p2)); pump(20);
    assert(log_data.snapshot_held); size_t old_length = log_data.snapshot_length;
    memcpy(saved, log_data.snapshot, old_length);
    assert(live_logger_append(&log_data, true, 3025));
    pump(2000);
    assert(p2.received_log_valid && p2.received_log_length == old_length);
    assert(!memcmp(received, saved, old_length));
    assert(completions == 1 && leases == 1 && releases == 1 && end_acks == 2);
    assert(!log_data.snapshot_held && log_data.running);
    assert(pico2_request_data(&p2)); pump(1000);
    assert(completions == 2 && memcmp(received, saved, old_length));
    assert(pico2_clear_log(&p2)); pump(100);
    assert(!log_data.running && !log_data.samples);
    assert(pico2_request_data(&p2)); pump(100);
    assert(p2.received_log_valid && p2.received_log_length == 0 && completions == 3);
    /* Abandoned retrieval releases the copy; a new request recovers unchanged data. */
    init_pair(); assert(live_logger_append(&log_data, true, 3000));
    drop_data_acks = true; assert(pico2_request_data(&p2)); pump(3000);
    assert(!log_data.snapshot_held && releases == 1 && !completions && !p2.received_log_valid);
    drop_data_acks = false; assert(pico2_request_data(&p2)); pump(1000);
    assert(p2.received_log_valid && completions == 1 && releases == 2);
    puts("PASS live RAM adapter: capacity, CONFIG units/duplicates, immutable snapshots, lost final ACK, retry exhaustion/recovery, CLEAR");
    return 0;
}
