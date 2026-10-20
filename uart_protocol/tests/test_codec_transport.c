#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "codec/temp_encoder.h"
#include "pico1/pico1_protocol.h"
#include "pico2/pico2_protocol.h"
#include "protocol/stream_parser.h"
static uart_inst_t u0 = {0}, u1 = {1};
static pico1_ctx_t p1; static pico2_ctx_t p2;
static struct { unsigned sender; size_t length; uint8_t bytes[MAX_STUFFED_FRAME_SIZE]; } queue[512];
static unsigned head, tail, completed, decoded, data_frames;
uint64_t time_us_64(void) { return 0; }
void uart_write_blocking(uart_inst_t *uart, const uint8_t *bytes, size_t length)
{
    assert(tail < 512); queue[tail].sender = uart->id; queue[tail].length = length;
    memcpy(queue[tail++].bytes, bytes, length);
}
static void complete(pico2_ctx_t *receiver)
{
    uint16_t count;
    assert(temp_decode(receiver->received_log, receiver->received_log_length, NULL, NULL, &count) == TEMP_DECODE_OK);
    decoded = count; completed++;
}
static void pump(void)
{
    while (head < tail)
    {
        unsigned n = head++; stream_parser_t parser; stream_parser_init(&parser);
        for (size_t i = 0; i < queue[n].length; i++)
        {
            if (stream_parser_feed(&parser, queue[n].bytes[i]))
            {
                protocol_frame_t frame; assert(frame_decode(parser.buffer, parser.length, &frame));
                if (queue[n].sender == 0) { pico1_handle_frame(&p1, &frame); }
                else
                {
                    if (frame.message_type == MSG_DATA) { data_frames++; }
                    pico2_handle_frame(&p2, &frame);
                }
            }
        }
    }
}
int main(void)
{
    temp_encoder_t encoder; temp_encoder_clear(&encoder);
    // Enough data to exercise multiple DATA frames, signed anchors and deltas.
    for (unsigned i = 0; i < 80; i++)
    {
        assert(temp_encoder_store(&encoder, (int16_t)(2400 + (int)(i % 7) * 25), NULL));
    }
    uint8_t snapshot[TEMP_BLOB_CAPACITY], received[TEMP_BLOB_CAPACITY]; size_t length;
    assert(temp_encoder_snapshot(&encoder, snapshot, sizeof(snapshot), &length));
    assert(pico1_init(&p1, (protocol_link_t){&u1, 0x11223344u, NULL}, snapshot, length, (pico1_hooks_t){0}));
    assert(pico2_init(&p2, (protocol_link_t){&u0, 0x11223344u, NULL}, received, sizeof(received),
                     1700000000u, 3600, (pico2_hooks_t){.transfer_complete = complete}));
    p2.state = P2_COMPLETE;
    assert(pico2_send_config(&p2)); pump(); assert(p1.configured);
    assert(pico2_request_data(&p2));
    assert(temp_encoder_store(&encoder, -2000, NULL)); // Snapshot stays frozen.
    pump();
    assert(completed == 1 && decoded == 80 && memcmp(received, snapshot, length) == 0);
    assert(data_frames == (length + DATA_CHUNK_SIZE - 1) / DATA_CHUNK_SIZE);
    assert(p1.logger_data_length == length); // No automatic clear.
    assert(pico2_request_data(&p2)); pump(); assert(completed == 2 && decoded == 80);
    assert(pico2_clear_log(&p2)); pump();
    temp_encoder_clear(&encoder); // Application reset after accepted CLEAR_LOG.
    assert(p1.logger_data_length == 0 && encoder.sample_count == 0);
    assert(pico2_request_data(&p2)); pump();
    assert(completed == 3 && decoded == 0 && p2.received_log_length == 0);
    puts("PASS codec UART transport: snapshot, multi-frame retrieval, repeat, clear, empty decode");
}
