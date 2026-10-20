#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "protocol/stream_parser.h"
#include "demo/demo.h"

// Wokwi: GP0 -> GP5, GP4 -> GP1. UART0 simulates Pico 2;
// UART1 simulates Pico 1. The serial monitor passively observes GP0.
#define PICO2_UART uart0
#define PICO2_TX 0
#define PICO2_RX 1
#define PICO1_UART uart1
#define PICO1_TX 4
#define PICO1_RX 5
#define BAUD_RATE 115200

static void debug_line(const char *text)
{
    // Text also reaches Pico 1; its delimiter parser ignores it outside frames.
    uart_puts(PICO2_UART, text);
}

static void init_uart(uart_inst_t *uart, uint tx, uint rx)
{
    uart_init(uart, BAUD_RATE); // Pico SDK default format is 8N1.
    gpio_set_function(tx, GPIO_FUNC_UART);
    gpio_set_function(rx, GPIO_FUNC_UART);
}

static void poll_pico1(pico1_ctx_t *p1, stream_parser_t *parser)
{
    while (uart_is_readable(p1->link.uart))
    {
        if (stream_parser_feed(parser, uart_getc(p1->link.uart)))
        {
            protocol_frame_t frame;
            if (frame_decode(parser->buffer, parser->length, &frame))
            {
                pico1_handle_frame(p1, &frame);
            }
            // Malformed frames are dropped; the sender recovers by timeout.
        }
    }
}

static void poll_pico2(pico2_ctx_t *p2, stream_parser_t *parser)
{
    while (uart_is_readable(p2->link.uart))
    {
        if (stream_parser_feed(parser, uart_getc(p2->link.uart)))
        {
            protocol_frame_t frame;
            if (frame_decode(parser->buffer, parser->length, &frame))
            {
                pico2_handle_frame(p2, &frame);
            }
            else
            {
                debug_line("\n[P2] Invalid frame dropped\n");
            }
        }
    }
}

int main(void)
{
    init_uart(PICO2_UART, PICO2_TX, PICO2_RX);
    init_uart(PICO1_UART, PICO1_TX, PICO1_RX);
    sleep_ms(1000);

    stream_parser_t pico1_parser;
    stream_parser_t pico2_parser;
    stream_parser_init(&pico1_parser);
    stream_parser_init(&pico2_parser);

    pico1_ctx_t p1;
    pico2_ctx_t p2;
    uint8_t receive_buffer[DEMO_RECEIVE_CAPACITY];
    bool reported = false;
    protocol_link_t sensor = {PICO1_UART, DEMO_SESSION_ID, debug_line};
    protocol_link_t receiver = {PICO2_UART, DEMO_SESSION_ID, debug_line};
    if (!demo_init(&p1, &p2, sensor, receiver, receive_buffer, sizeof(receive_buffer)))
    {
        debug_line("\n=== UART PROTOCOL FAILURE TEST FAILED ===\n");
        while (true) { sleep_ms(1); }
    }
    demo_start(&p2);

    while (true)
    {
        poll_pico1(&p1, &pico1_parser);
        poll_pico2(&p2, &pico2_parser);
        pico1_tick(&p1);
        pico2_tick(&p2);
        demo_report_result(&p2, &reported);
        sleep_ms(1);
    }
}
