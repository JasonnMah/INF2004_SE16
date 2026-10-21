#ifndef WIFI_PROTOCOL_UART_ADAPTER_H
#define WIFI_PROTOCOL_UART_ADAPTER_H
#include <stdint.h>
#include <stddef.h>
typedef struct { unsigned unit; } uart_inst_t;
extern uart_inst_t protocol_uart0, protocol_uart1;
#define uart0 (&protocol_uart0)
#define uart1 (&protocol_uart1)
void uart_write_blocking(uart_inst_t *uart, const uint8_t *data, size_t length);
#endif
