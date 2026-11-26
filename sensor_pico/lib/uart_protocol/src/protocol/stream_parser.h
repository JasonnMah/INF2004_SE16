#ifndef PROTOCOL_STREAM_PARSER_H
#define PROTOCOL_STREAM_PARSER_H

#include "protocol.h"

typedef struct
{
    uint8_t buffer[MAX_STUFFED_FRAME_SIZE];
    size_t length;
    bool receiving;

} stream_parser_t;

void stream_parser_init(stream_parser_t *parser);
bool stream_parser_feed(stream_parser_t *parser, uint8_t byte);
#endif
