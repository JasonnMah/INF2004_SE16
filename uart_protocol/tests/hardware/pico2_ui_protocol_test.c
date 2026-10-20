// Single physical Pico W: UART0 acts as Pico 2, UART1 as the Pico 1 endpoint.
// Jumpers: GP0 (TX0) -> GP5 (RX1), GP4 (TX1) -> GP1 (RX0).
// Diagnostics use USB only; no readable text is injected into either UART.
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "hardware/uart.h"
#include "protocol/stream_parser.h"
#include "pico1/pico1_protocol.h"
#include "pico2/pico2_ui.h"
#include "demo/mock_logger.h"

#define SESSION_ID 0x11223344u
#define CONFIG_TIMESTAMP 1700000000u // Same fixed timestamp as demo_init().
#define SAMPLING_INTERVAL 3600u

static void usb_log(const char *text)
{
    printf("%s", text);
}

// The two endpoints share this chip. Surface sender retry exhaustion locally;
// this is not an over-the-wire failure notification for a future two-board link.
static pico2_ctx_t *receiver;
static pico1_ctx_t *sensor;
static bool validate_log(const uint8_t *data, size_t length)
{
    // Compare with the endpoint's CURRENT logical log, including after clear.
    // The original caller-owned bytes remain intact when its length becomes zero.
    return length == sensor->logger_data_length &&
           (length == 0 || memcmp(data, sensor->logger_data, length) == 0);
}
static void transfer_failed(void)
{
    if (receiver->state == P2_RECEIVING_DATA) { pico2_abort(receiver); }
}
static void transfer_complete(pico2_ctx_t *p2)
{
    if (p2->received_log_length == 0)
    {
        protocol_log(&p2->link, "[P2] Retrieved log is empty (0 bytes)\n");
    }
    else
    {
        protocol_log(&p2->link, "[P2] Retrieved compressed log matches Pico 1 data\n");
    }
    // Deliberately no CLEAR_LOG: GP21 can retrieve the same source again.
}

static void init_uart(uart_inst_t *uart, uint tx, uint rx)
{
    uart_init(uart, 115200);
    gpio_set_function(tx, GPIO_FUNC_UART);
    gpio_set_function(rx, GPIO_FUNC_UART);
    uart_set_format(uart, 8, 1, UART_PARITY_NONE);
}

static void poll_sensor(pico1_ctx_t *p1, stream_parser_t *parser)
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
        }
    }
}

static void poll_receiver(pico2_ctx_t *p2, stream_parser_t *parser)
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
        }
    }
}

int main(void)
{
    stdio_init_all();
    init_uart(uart0, 0, 1);
    init_uart(uart1, 4, 5);

    pico1_ctx_t p1;
    pico2_ctx_t p2;
    pico2_ui_t ui;
    uint8_t receive_buffer[DEMO_RECEIVE_CAPACITY];
    receiver = &p2;
    sensor = &p1;
    // Both peers already share the demo session. Pico 1 does not require HELLO
    // before CONFIG. Only this test endpoint knows the expected mock bytes.
    if (!pico1_init(&p1, (protocol_link_t){uart1, SESSION_ID, usb_log},
                    mock_logger_data(), mock_logger_length(),
                    (pico1_hooks_t){.transfer_failed = transfer_failed}) ||
        !pico2_init(&p2, (protocol_link_t){uart0, SESSION_ID, usb_log},
                    receive_buffer, sizeof(receive_buffer),
                    CONFIG_TIMESTAMP, SAMPLING_INTERVAL, (pico2_hooks_t){
                        .validate_log = validate_log,
                        .transfer_complete = transfer_complete}))
    {
        return 1;
    }
    p2.state = P2_COMPLETE; // Idle until a physical GP20 press starts a command.
    pico2_ui_init(&ui, &p2);
    stream_parser_t sensor_parser, receiver_parser;
    stream_parser_init(&sensor_parser);
    stream_parser_init(&receiver_parser);
    bool announced = false;

    while (true)
    {
        // Defer the startup banner until USB opens, without blocking UART/UI.
        // Print it again on reconnect so the terminal always shows instructions.
        if (!stdio_usb_connected())
        {
            announced = false;
        }
        else if (!announced)
        {
            puts("Pico 2 UI protocol test ready");
            puts("GP20: send CONFIG");
            puts("GP21: retrieve data");
            puts("GP22: clear log");
            announced = true;
        }

        poll_sensor(&p1, &sensor_parser);
        poll_receiver(&p2, &receiver_parser);
        pico1_tick(&p1);
        // Once a command fails, leave it stopped until a NEW button event.
        // The unchanged protocol tick supplies all timeout/retry decisions.
        if ((ui.config_pending || ui.retrieval_pending || ui.clear_pending) && p2.state != P2_FAILED)
        {
            pico2_tick(&p2);
        }
        pico2_ui_tick(&ui);
        sleep_ms(1);
    }
}
