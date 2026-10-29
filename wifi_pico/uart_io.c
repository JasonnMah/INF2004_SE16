#include "app.h"
#include "uart_io.h"
unsigned protocol_receive(unsigned unit, stream_parser_t *parser,
                          void (*handle)(const protocol_frame_t *))
{
    /* A fixed budget prevents a noisy peer from monopolizing its core/task. */
    for (unsigned batch = 0; batch < 8; ++batch) {
        unsigned char bytes[64];
        unsigned fault = 0;
        unsigned count = rt_rx_take(unit, bytes, sizeof(bytes), &fault);
        if (fault) {
            stream_parser_init(parser);
            return fault;
        }
        for (unsigned i = 0; i < count; ++i) {
            if (stream_parser_feed(parser, bytes[i])) {
                protocol_frame_t frame;
                if (frame_decode(parser->buffer, parser->length, &frame)) {
                    handle(&frame);
                } else {
                    /* Preserve baseline behavior: discard invalid frames and
                     * let the existing sender timeout/NACK machinery recover. */
                    rt_numbers("[UART%u] CRC/frame rejected\n", unit, 0, 0);
                }
            }
        }
        if (count < sizeof(bytes)) { break; }
    }
    return 0;
}

/* Outbound callbacks used by the same shared protocol implementation. */
static unsigned units[] = {0, 1};
static uint64_t now_ms(void *context) { (void)context; return rt_now_ms(); }
static void write_bytes(void *context, const uint8_t *bytes, size_t length)
{
    unsigned unit = *(unsigned *)context;
#if APP_FAULT_INJECTION
    if (fault_drop_tx(unit, bytes, length)) { return; }
#endif
    rt_uart_write(unit, bytes, (unsigned)length);
}
protocol_link_t app_protocol_link(unsigned unit)
{
    return (protocol_link_t){.context = &units[unit], .session_id = 0x11223344u,
        .log = rt_log, .write = write_bytes, .now_ms = now_ms};
}
