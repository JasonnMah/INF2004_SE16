// Real Sensor-side board: protocol UART1 only; diagnostics use USB.
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "hardware/uart.h"
#include "protocol/stream_parser.h"
#include "pico1/pico1_protocol.h"
#include "demo/mock_logger.h"

#define SESSION_ID 0x11223344u

static void usb_log(const char *text) { printf("%s", text); }

int main(void)
{
    stdio_init_all();
    uart_init(uart1, 115200);
    gpio_set_function(4, GPIO_FUNC_UART);
    gpio_set_function(5, GPIO_FUNC_UART);
    uart_set_format(uart1, 8, 1, UART_PARITY_NONE);

    pico1_ctx_t p1;
    // Member 1 integration seam: replace these two mock accessors with the
    // compressed buffer pointer and valid byte length. Keep it stable in flight.
    if (!pico1_init(&p1, (protocol_link_t){uart1, SESSION_ID, usb_log},
                    mock_logger_data(), mock_logger_length(), (pico1_hooks_t){0}))
    {
        return 1;
    }
    stream_parser_t parser;
    stream_parser_init(&parser);
    bool announced = false;
    while (true)
    {
        if (!stdio_usb_connected()) { announced = false; }
        else if (!announced)
        {
            puts("Sensor Pico protocol test ready");
            printf("Logger length: %u bytes\n", (unsigned)p1.logger_data_length);
            announced = true;
        }
        while (uart_is_readable(uart1))
        {
            if (stream_parser_feed(&parser, uart_getc(uart1)))
            {
                protocol_frame_t frame;
                if (frame_decode(parser.buffer, parser.length, &frame))
                {
                    pico1_handle_frame(&p1, &frame);
                }
            }
        }
        pico1_tick(&p1);
        sleep_ms(1);
    }
}
