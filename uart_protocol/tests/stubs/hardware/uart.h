#ifndef TEST_UART_H
#define TEST_UART_H
#include <stddef.h>
#include <stdint.h>
typedef struct { unsigned id; } uart_inst_t;
void uart_write_blocking(uart_inst_t *uart, const uint8_t *data, size_t length);
#endif
