// Real Wi-Fi-side board: protocol UART0 only; diagnostics use USB.
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "hardware/uart.h"
#include "protocol/stream_parser.h"
#include "pico2/pico2_ui.h"

#define SESSION_ID 0x11223344u
#define CONFIG_TIMESTAMP 1700000000u
#define SAMPLING_INTERVAL 3600u
// Total receive storage is independent of the per-frame payload limit.
#define RECEIVE_CAPACITY 256u

static void usb_log(const char *text) { printf("%s", text); }

static void transfer_complete(pico2_ctx_t *p2)
{
    if (p2->received_log_length == 0)
    {
        puts("[P2] Retrieved log is empty (0 bytes)");
    }
    else
    {
        printf("[P2] Retrieved log: %u bytes\n", (unsigned)p2->received_log_length);
    }
    // No source-data oracle and no automatic CLEAR_LOG on this board.
}

int main(void)
{
    stdio_init_all();
    uart_init(uart0, 115200);
    gpio_set_function(0, GPIO_FUNC_UART);
    gpio_set_function(1, GPIO_FUNC_UART);
    uart_set_format(uart0, 8, 1, UART_PARITY_NONE);

    pico2_ctx_t p2;
    pico2_ui_t ui;
    uint8_t received[RECEIVE_CAPACITY];
    if (!pico2_init(&p2, (protocol_link_t){uart0, SESSION_ID, usb_log},
                    received, sizeof(received), CONFIG_TIMESTAMP, SAMPLING_INTERVAL,
                    (pico2_hooks_t){.transfer_complete = transfer_complete}))
    {
        return 1;
    }
    // Same pre-agreed session as the verified UI test; no HELLO is required.
    p2.state = P2_COMPLETE;
    pico2_ui_init(&ui, &p2);
    stream_parser_t parser;
    stream_parser_init(&parser);
    bool announced = false;
    while (true)
    {
        if (!stdio_usb_connected()) { announced = false; }
        else if (!announced)
        {
            puts("Wi-Fi Pico ready");
            puts("GP20: send CONFIG");
            puts("GP21: retrieve data");
            puts("GP22: clear log");
            announced = true;
        }
        while (uart_is_readable(uart0))
        {
            if (stream_parser_feed(&parser, uart_getc(uart0)))
            {
                protocol_frame_t frame;
                if (frame_decode(parser.buffer, parser.length, &frame))
                {
                    pico2_handle_frame(&p2, &frame);
                }
            }
        }
        // Do not keep ticking a failed command; a new button event can retry.
        if ((ui.config_pending || ui.retrieval_pending || ui.clear_pending) &&
            p2.state != P2_FAILED)
        {
            pico2_tick(&p2);
        }
        pico2_ui_tick(&ui);
        sleep_ms(1);
    }
}
