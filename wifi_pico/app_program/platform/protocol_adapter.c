/* ABI adapters only; framing and reliability compile from uart_protocol unchanged. */
#include "hardware/uart.h"
#include "pico/stdlib.h"
#include "app.h"
#include "protocol_io.h"
uart_inst_t protocol_uart0 = {0}, protocol_uart1 = {1};
uint64_t time_us_64(void) { return (uint64_t)rt_now_ms() * 1000u; }
void uart_write_blocking(uart_inst_t *uart, const uint8_t *data, size_t length)
{
    if (!fault_drop_tx(uart->unit, data, length)) {
        rt_uart_write(uart->unit, data, (unsigned)length);
    }
}
