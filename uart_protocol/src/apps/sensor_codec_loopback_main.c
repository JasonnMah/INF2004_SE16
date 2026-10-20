// Single Pico W: UART0=Pico 2, UART1=Pico 1, AHT10 on I2C0 GP8/GP9.
#include <stdio.h>
#include <limits.h>
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "hardware/uart.h"
#include "hardware/i2c.h"
#include "protocol/stream_parser.h"
#include "pico1/pico1_protocol.h"
#include "pico2/pico2_ui.h"
#include "codec/temp_encoder.h"
#include "codec/temp_log_print.h"

#ifndef CODEC_SAMPLE_INTERVAL
#define CODEC_SAMPLE_INTERVAL 3600u
#endif
#ifndef CODEC_STARTUP_DIAGNOSTICS
#define CODEC_STARTUP_DIAGNOSTICS 0
#endif
#ifndef CODEC_RX_TIMEOUT_TEST
#define CODEC_RX_TIMEOUT_TEST 0
#endif
#define SESSION_ID 0x11223344u
#define AHT10_ADDR 0x38u
static pico1_ctx_t p1;
static pico2_ctx_t p2;
static pico2_ui_t ui;
static temp_encoder_t logger;
static uint8_t snapshot[TEMP_BLOB_CAPACITY], received[TEMP_BLOB_CAPACITY];
static bool logging_enabled, measurement_pending;
static uint64_t next_sample_ms, measurement_ready_ms;
#if CODEC_RX_TIMEOUT_TEST
static bool suppress_next_retrieval = true;
static bool suppress_transfer_frames;
#endif
static void usb_log(const char *text) { printf("%s", text); }

static void boot_checkpoint(const char *text)
{
#if CODEC_STARTUP_DIAGNOSTICS
    printf("[BOOT] %s\n", text);
    stdio_flush();
#else
    (void)text;
#endif
}

static void transfer_failed(void)
{
#if CODEC_RX_TIMEOUT_TEST
    // This experiment must exercise the remote receiver watchdog, rather than
    // the earlier single-board shortcut from sender failure to receiver abort.
    if (suppress_transfer_frames) { return; }
#endif
    if (p2.state == P2_RECEIVING_DATA) { pico2_abort(&p2); }
}
static void transfer_complete(pico2_ctx_t *receiver)
{
    temp_log_print(receiver->received_log, receiver->received_log_length,
                   receiver->config_timestamp, receiver->config_interval);
}
static void sensor_tick(uint64_t now)
{
    if (!logging_enabled) { return; }
    if (!measurement_pending)
    {
        if (now < next_sample_ms) { return; }
        // Preserve Member 1 trigger/conversion; replace its sleep with a deadline
        // so both UART endpoints and button/retry processing keep running.
        uint8_t trigger[] = {0xac, 0x33, 0x00};
        next_sample_ms = now + (uint64_t)p1.sampling_interval * 1000u;
        if (i2c_write_timeout_us(i2c0, AHT10_ADDR, trigger, 3, false, 5000) != 3)
        {
            puts("[ENCODE] AHT10 trigger failed; sample not counted");
            return;
        }
        measurement_pending = true;
        measurement_ready_ms = now + 80;
        return;
    }
    if (now < measurement_ready_ms) { return; }
    measurement_pending = false;
    uint8_t data[6];
    if (i2c_read_timeout_us(i2c0, AHT10_ADDR, data, 6, false, 5000) != 6 || (data[0] & 0x80))
    {
        puts("[ENCODE] AHT10 read failed/busy; sample not counted");
        return;
    }
    uint32_t raw = ((uint32_t)(data[3] & 0x0f) << 16) | ((uint32_t)data[4] << 8) | data[5];
    int64_t numerator = (int64_t)raw * 20000 - (int64_t)5000 * 1048576;
    int16_t t = (int16_t)((numerator + (numerator >= 0 ? 524288 : -524288)) / 1048576);
    temp_decoded_sample_t encoding;
    if (temp_encoder_store(&logger, t, &encoding)) { temp_encode_print(t, &encoding); }
    else
    {
        puts("[ENCODE] Buffer full; sample not counted (logging paused)");
        logging_enabled = false;
    }
}
static void sensor_frame(const protocol_frame_t *frame)
{
    bool duplicate = p1.last_command_valid && p1.last_command_sequence == frame->sequence &&
                     p1.last_command_type == frame->message_type;
    pico1_handle_frame(&p1, frame);
    // Only react to a command the existing protocol actually accepted; retries
    // must not restart acquisition or clear newly collected samples again.
    bool accepted = !duplicate && frame->session_id == p1.link.session_id &&
                    p1.last_command_valid && p1.last_command_sequence == frame->sequence &&
                    p1.last_command_type == frame->message_type;
#if CODEC_RX_TIMEOUT_TEST
    if (accepted && frame->message_type == MSG_REQUEST_DATA && suppress_next_retrieval)
    {
        suppress_next_retrieval = false;
        suppress_transfer_frames = true;
        puts("[TEST] Suppressing DATA/TRANSFER_END for this retrieval; REQUEST ACK is delivered");
    }
#endif
    if (accepted && frame->message_type == MSG_CONFIG)
    {
        logging_enabled = true;
        measurement_pending = false;
        next_sample_ms = time_us_64() / 1000u;
    }
    else if (accepted && frame->message_type == MSG_CLEAR_LOG)
    {
        temp_encoder_clear(&logger);
        // Leave the cleared test log empty until a new CONFIG starts acquisition.
        logging_enabled = measurement_pending = false;
        puts("[ENCODE] Logger cleared; press GP20 to start sampling again");
    }
}
static void poll_uart(uart_inst_t *uart, stream_parser_t *parser, bool sensor)
{
    while (uart_is_readable(uart))
    {
        if (stream_parser_feed(parser, uart_getc(uart)))
        {
            protocol_frame_t frame;
            if (frame_decode(parser->buffer, parser->length, &frame))
            {
                if (sensor) { sensor_frame(&frame); }
                else
                {
#if CODEC_RX_TIMEOUT_TEST
                    // Consume the bytes normally; only omit dispatch of transfer
                    // content. UART polling, command ACKs and both clocks run.
                    if (suppress_transfer_frames && frame.session_id == SESSION_ID &&
                        (frame.message_type == MSG_DATA || frame.message_type == MSG_TRANSFER_END))
                    {
                        continue;
                    }
#endif
                    pico2_handle_frame(&p2, &frame);
                }
            }
        }
    }
}
int main(void)
{
    stdio_init_all();
#if CODEC_STARTUP_DIAGNOSTICS
    // Diagnostic image only: keep sensor/UART initialization behind USB open
    // so an early assertion or hardware stall cannot hide its last checkpoint.
    // No protocol command is in flight yet. Normal builds remain headless.
    while (!stdio_usb_connected()) { sleep_ms(10); }
    sleep_ms(300); // Give the host time to start receiving after asserting DTR.
#endif
    boot_checkpoint("codec startup diagnostics v1; USB output ready");
    boot_checkpoint("I2C0 setup begin: SDA=GP8 SCL=GP9, 100 kHz");
    i2c_init(i2c0, 100000);
    gpio_set_function(8, GPIO_FUNC_I2C); gpio_set_function(9, GPIO_FUNC_I2C);
    gpio_pull_up(8); gpio_pull_up(9);
    sleep_ms(20);
    uint8_t init[] = {0xe1, 0x08, 0x00};
    boot_checkpoint("AHT10 init write begin: address=0x38, timeout=5000 us");
    int sensor_init_result = i2c_write_timeout_us(i2c0, AHT10_ADDR, init, 3, false, 5000);
    bool sensor_ready = sensor_init_result == 3;
#if CODEC_STARTUP_DIAGNOSTICS
    printf("[BOOT] AHT10 init write returned %d (3=ACKed all init bytes)\n", sensor_init_result);
    stdio_flush();
#endif
    sleep_ms(10);
    boot_checkpoint("UART0 init begin");
    uart_init(uart0, 115200);
    boot_checkpoint("UART0 initialized; UART1 init begin");
    uart_init(uart1, 115200);
    boot_checkpoint("UART1 initialized; assigning GP0/GP1/GP4/GP5 and 8N1");
    gpio_set_function(0, GPIO_FUNC_UART); gpio_set_function(1, GPIO_FUNC_UART);
    gpio_set_function(4, GPIO_FUNC_UART); gpio_set_function(5, GPIO_FUNC_UART);
    uart_set_format(uart0, 8, 1, UART_PARITY_NONE);
    uart_set_format(uart1, 8, 1, UART_PARITY_NONE);
    boot_checkpoint("UART setup complete; protocol contexts init begin");
    if (!pico1_init(&p1, (protocol_link_t){uart1, SESSION_ID, usb_log}, snapshot, 0,
                    (pico1_hooks_t){.transfer_failed = transfer_failed}) ||
        !pico2_init(&p2, (protocol_link_t){uart0, SESSION_ID, usb_log}, received, sizeof(received),
                    1700000000u, CODEC_SAMPLE_INTERVAL,
                    (pico2_hooks_t){.transfer_complete = transfer_complete}))
    {
        boot_checkpoint("ERROR: protocol context initialization failed");
        return 1;
    }
    boot_checkpoint("Protocol contexts ready; button/LED init begin");
    p2.state = P2_COMPLETE;
    pico2_ui_init(&ui, &p2);
    stream_parser_t sensor_parser, receiver_parser;
    stream_parser_init(&sensor_parser); stream_parser_init(&receiver_parser);
    boot_checkpoint("Entering main loop");
    bool announced = false;
    while (true)
    {
        if (!stdio_usb_connected()) { announced = false; }
        else if (!announced)
        {
            puts("Sensor codec loopback ready");
            printf("AHT10 init: %s; sample interval: %u seconds\n",
                   sensor_ready ? "OK" : "FAILED", (unsigned)CODEC_SAMPLE_INTERVAL);
            puts("GP20: CONFIG/start sampling; GP21: retrieve/decode; GP22: clear/pause");
#if CODEC_RX_TIMEOUT_TEST
            puts("[TEST] RX timeout test armed: first GP21 retrieval is suppressed; later retrievals are normal");
#endif
            announced = true;
        }
        // Acquisition may continue, but the borrowed snapshot cannot change
        // between first DATA and final END ACK (including retransmissions).
        if (p1.transfer_state == P1_TRANSFER_IDLE && !p1.pending_transfer.active)
        {
            size_t length;
            if (temp_encoder_snapshot(&logger, snapshot, sizeof(snapshot), &length))
            {
                p1.logger_data_length = length;
            }
        }
        poll_uart(uart1, &sensor_parser, true);
        poll_uart(uart0, &receiver_parser, false);
        pico1_tick(&p1);
        if ((ui.config_pending || ui.retrieval_pending || ui.clear_pending) && p2.state != P2_FAILED)
        {
            pico2_tick(&p2);
        }
        pico2_ui_tick(&ui);
#if CODEC_RX_TIMEOUT_TEST
        if (suppress_transfer_frames && p2.state == P2_FAILED)
        {
            suppress_transfer_frames = false;
            printf("[TEST] RX suppression released; received_log_valid=%u, received_bytes=%u\n",
                   (unsigned)p2.received_log_valid, (unsigned)p2.received_log_length);
            puts("[TEST] Wait for failure flashes, then press GP21 again for normal retrieval");
        }
#endif
        sensor_tick(time_us_64() / 1000u);
        sleep_ms(1);
    }
}
