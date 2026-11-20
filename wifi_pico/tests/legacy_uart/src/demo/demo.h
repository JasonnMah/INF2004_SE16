#ifndef DEMO_DEMO_H
#define DEMO_DEMO_H

#include "pico1/pico1_protocol.h"
#include "pico2/pico2_protocol.h"
#include "mock_logger.h"

#define DEMO_SESSION_ID 0x11223344u

bool demo_init(pico1_ctx_t *p1, pico2_ctx_t *p2,
               protocol_link_t sensor, protocol_link_t receiver,
               uint8_t *receive_buffer, size_t receive_capacity);
void demo_start(pico2_ctx_t *p2);
void demo_report_result(pico2_ctx_t *p2, bool *reported);

#endif
