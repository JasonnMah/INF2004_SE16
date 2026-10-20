#include "stream_parser.h"

void stream_parser_init(stream_parser_t *parser)
{
    parser->length = 0;
    parser->receiving = false;
}

bool stream_parser_feed(stream_parser_t *parser, uint8_t byte)
{
    if (!parser->receiving)
    {
        if (byte == SOF)
        {
            parser->receiving = true;
            parser->length = 0;
            parser->buffer[parser->length++] = byte;
        }

        return false;
    }

    if (parser->length >= MAX_STUFFED_FRAME_SIZE)
    {
        stream_parser_init(parser);
        // The byte that reveals overflow can itself start the next frame.
        if (byte == SOF)
        {
            parser->receiving = true;
            parser->buffer[parser->length++] = byte;
        }
        return false;
    }

    // Consecutive delimiters are an empty boundary, not a frame. This also
    // restores alignment after a truncated frame consumes the next SOF as EOF.
    if (byte == SOF && parser->length == 1)
    {
        return false;
    }

    parser->buffer[parser->length++] = byte;

    if (byte == SOF)
    {
        parser->receiving = false;
        return true;
    }

    return false;
}
