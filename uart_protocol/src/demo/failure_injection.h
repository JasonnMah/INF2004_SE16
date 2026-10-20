#ifndef DEMO_FAILURE_INJECTION_H
#define DEMO_FAILURE_INJECTION_H

#include "pico1/pico1_protocol.h"
#include "pico2/pico2_protocol.h"

#define TEST_NONE                 0
#define TEST_CORRUPT_FIRST_DATA   1
#define TEST_DROP_FIRST_DATA_ACK  2
#define TEST_WRONG_FIRST_BLOCK    3
#define TEST_DROP_CONFIG_ACK      4


#ifndef PROTOCOL_TEST
#define PROTOCOL_TEST TEST_DROP_CONFIG_ACK
#endif
#if PROTOCOL_TEST < TEST_NONE || PROTOCOL_TEST > TEST_DROP_CONFIG_ACK
#error "PROTOCOL_TEST must be 0..4"
#endif

void failure_injection_reset(void (*log)(const char *));
bool failure_send_data(const protocol_link_t *link, const pending_tx_t *pending,
                        uint16_t block, const uint8_t *payload, uint16_t length);
bool failure_drop_config_ack(void);
bool failure_drop_data_ack(uint16_t block);
void failure_print_banner(void);

#endif
