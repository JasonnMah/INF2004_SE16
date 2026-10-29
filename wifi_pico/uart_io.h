#ifndef WIFI_UART_IO_H
#define WIFI_UART_IO_H
#include "protocol/stream_parser.h"
#include "protocol/frame.h"
#include "protocol/link.h"
protocol_link_t app_protocol_link(unsigned unit);
unsigned protocol_receive(unsigned unit, stream_parser_t *parser,
                          void (*handle)(const protocol_frame_t *));
void fault_observe_sensor(const protocol_frame_t *frame);
void fault_observe_gateway(const protocol_frame_t *frame);
bool fault_drop_rx(const protocol_frame_t *frame);
bool fault_drop_tx(unsigned unit, const uint8_t *bytes, size_t length);
void fault_reset(void);
#endif
