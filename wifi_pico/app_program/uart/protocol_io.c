#include "app.h"
#include "protocol_io.h"
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
