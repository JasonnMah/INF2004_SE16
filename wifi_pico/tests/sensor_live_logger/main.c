/* Standalone, removable bare-metal Sensor integration test.
 * Reuses JZ's aht10.c and encoder.c READ ONLY and the existing shared protocol.
 * No flash, RTC, Wi-Fi, RTOS or Sensor buttons. Wi-Fi Pico buttons send commands.
 *
 * This file runs on the SENSOR Pico, even though it lives under wifi_pico/tests.
 * CONFIG starts timed AHT10 readings. REQUEST_DATA sends a fixed copy of the
 * compressed RAM log. CLEAR empties that RAM log and pauses further readings.
 * The shared UART library builds frames, checks CRCs and handles ACKs/retries.
 */
#include <stdio.h>
#include <inttypes.h>
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "hardware/i2c.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/uart.h"
#include "aht10.h"
#ifdef SENSOR_I2C_DIAGNOSTICS
#include "i2c_diag.h"
#endif
#include "live_logger.h"
#include "pico1/pico1_protocol.h"
#include "protocol/stream_parser.h"
#include "protocol/crc16.h"

/* On the Sensor board: GP4 TX -> Wi-Fi GP1 RX; GP5 RX <- Wi-Fi GP0 TX.
 * AHT10 uses the Sensor board's GP0/GP1, which are separate from GP4/GP5. */
#define SENSOR_UART uart1
#define SENSOR_TX 4u
#define SENSOR_RX 5u
#define SENSOR_SDA 0u
#define SENSOR_SCL 1u
#define SESSION_ID 0x11223344u
/* Buffer bytes during sensor waits; limit work per loop so other jobs get time. */
#define RX_CAPACITY 512u
#define RX_POLL_BUDGET 128u
#define USB_SETTLE_MS 200u

/* These objects live for the whole program; no memory is allocated at runtime. */
static live_logger_t logger;
static pico1_ctx_t endpoint;
static stream_parser_t parser;
static protocol_frame_t frame;
/* The UART interrupt adds bytes at rx_head; the main loop removes at rx_tail.
 * volatile makes the compiler reread counters that the interrupt can change. */
static uint8_t rx[RX_CAPACITY];
static volatile uint32_t rx_head, rx_tail, rx_errors, rx_dropped;
static uint32_t frames_received, rejected_frames, snapshot_count;
static uint64_t next_sample_ms;
static aht10_status_t init_status, last_read_status;
static int16_t last_temperature;
static bool have_reading;
static size_t dump_offset;
static bool dumping;

/* The protocol needs elapsed milliseconds for retries, not a calendar clock. */
static uint64_t now_ms(void *context)
{
    (void)context;
    return time_us_64() / 1000u;
}

/* Send bytes already prepared by the shared protocol; do not build frames here. */
static void send_bytes(void *context, const uint8_t *bytes, size_t length)
{
    (void)context;
    uart_write_blocking(SENSOR_UART, bytes, length);
}

/* UART operation must continue even when no USB Serial Monitor is open. */
static void protocol_message(const char *text)
{
    if (stdio_usb_connected()) printf("%s", text);
}

/* AHT10 conversion waits are bounded but can last ~200 ms. Collect UART RX in
 * an ISR during that wait; parse frames and run the protocol only in main(). */
static void uart_rx_irq(void)
{
    while (uart_is_readable(SENSOR_UART)) {
        uint32_t value = uart_get_hw(SENSOR_UART)->dr;
        if (value & 0xf00u) {
            ++rx_errors;
            uart_get_hw(SENSOR_UART)->rsr = 0;
        } else if (rx_head - rx_tail == RX_CAPACITY) {
            ++rx_dropped;
        } else {
            rx[rx_head % RX_CAPACITY] = (uint8_t)value;
            ++rx_head;
        }
    }
}

/* Remove one saved UART byte without racing the receive interrupt. */
static bool pop_byte(uint8_t *byte)
{
    /* Single core: this short critical section protects ISR/main ownership.
     * There are no flash operations or second core in this test. */
    uint32_t state = save_and_disable_interrupts();
    bool available = rx_tail != rx_head;
    if (available) *byte = rx[rx_tail++ % RX_CAPACITY];
    restore_interrupts(state);
    return available;
}

/* Called after the protocol accepts CONFIG. Time comes from the command;
 * this test has no external RTC. Schedule the first sample immediately. */
static bool configure(void *context, uint32_t timestamp, uint32_t seconds)
{
    if (!live_logger_configure(context, timestamp, seconds)) return false;
    next_sample_ms = now_ms(NULL); /* First real reading follows the CONFIG ACK. */
    if (stdio_usb_connected())
        printf("[CONFIG] interval=%" PRIu32 " seconds; timestamp=%" PRIu32 " (demo, no RTC)\n",
               seconds, timestamp);
    return true;
}

/* A snapshot is a fixed copy of the log. Retries must send those same bytes,
 * even if new sensor readings are added to the working log during retrieval. */
static bool snapshot(void *context, const uint8_t **bytes, size_t *length)
{
    dumping = false; /* A new retrieval must not mix generations in a USB dump. */
    if (!live_logger_snapshot(context, bytes, length)) return false;
    ++snapshot_count;
    if (stdio_usb_connected())
        printf("[LOG] Snapshot=%" PRIu32 " bytes=%u crc16_x25=%04x; RAM only\n",
               snapshot_count, (unsigned)*length, crc16_x25(*bytes, *length));
    return true;
}

/* READY tells the protocol it may ACK CLEAR now. This only clears RAM;
 * a future flash version must finish and verify its erase before returning READY. */
static pico1_storage_result_t clear_log(void *context)
{
    if (!live_logger_clear(context)) return P1_STORAGE_ERROR;
    dumping = false;
    snapshot_count = 0;
    if (stdio_usb_connected()) puts("[CLEAR] RAM log cleared; sampling paused; no flash erased");
    return P1_STORAGE_READY;
}

/* The protocol calls this after final ACK or an abandoned transfer. Releasing
 * the fixed copy allows another retrieval; it does not erase the working log. */
static void release_log(void *context, bool completed)
{
    live_logger_release(context);
    if (stdio_usb_connected())
        printf("[LOG] Snapshot released: %s\n", completed ? "final ACK received" : "transfer aborted");
}

/* Reused on USB connection and on 's'. A repeated START line here is a status
 * reprint and does not by itself mean that the Pico restarted. */
static void print_status(void)
{
    puts("[START] Sensor live AHT10 UART test; bare-metal, RAM only");
    printf("[SENSOR] AHT10 init: %s; I2C0 SDA=GP0 SCL=GP1 address=0x38\n", aht10_status_str(init_status));
    #ifdef SENSOR_I2C_DIAGNOSTICS
    sensor_i2c_trace_report();
    #endif
    puts("[READY] UART1 GP4 TX / GP5 RX; 115200 8N1; session=11223344");
    printf("[LOG] samples=%" PRIu32 " errors=%" PRIu32 " running=%u full=%u capacity=%u bytes\n",
           logger.samples, logger.read_errors, logger.running ? 1u : 0u,
           logger.full ? 1u : 0u, (unsigned)LIVE_LOG_CAPACITY);
    printf("[UART] frames=%" PRIu32 " rejected=%" PRIu32 " errors=%" PRIu32 " dropped=%" PRIu32 "\n",
           frames_received, rejected_frames, rx_errors, rx_dropped);
    if (have_reading) {
        int magnitude = last_temperature < 0 ? -last_temperature : last_temperature;
        printf("[SENSOR] latest=%s%d.%02d C (raw, before 0.25 C quantization)\n",
               last_temperature < 0 ? "-" : "", magnitude / 100, magnitude % 100);
    }
    puts("[MODE] JZ encoder pages=256 bytes; format=interval12 (1..4095 minutes); flash=OFF RTC=OFF; CONFIG timestamp is simulated");
    puts("[READY] Wi-Fi GP20 starts sampling; interval comes from CONFIG (existing Wi-Fi: 3600 seconds)");
    puts("[USB] s + Enter: status; d + Enter: last UART snapshot hex; DTR off/on: status");
}

/* Check USB without waiting for input: 's' shows status, 'd' dumps the last
 * retrieval copy as hex. Neither command requests a new AHT10 reading. */
static void console_poll(void)
{
    static bool was_connected;
    static uint64_t announce_at;
    bool connected = stdio_usb_connected();
    if (!connected) {
        was_connected = false;
        announce_at = 0;
        dumping = false;
        return;
    }
    if (!was_connected) {
        if (!announce_at) announce_at = now_ms(NULL) + USB_SETTLE_MS;
        if (now_ms(NULL) < announce_at) return;
        print_status();
        was_connected = true;
    }
    for (unsigned n = 0; n < 16; ++n) {
        int character = getchar_timeout_us(0);
        if (character == PICO_ERROR_TIMEOUT) break;
        if (character == 's' || character == 'S') print_status();
        if (character == 'd' || character == 'D') {
            if (!snapshot_count) { puts("[USB] No UART snapshot yet; retrieve first"); continue; }
            printf("[BINARY BEGIN] completion=%" PRIu32 " length=%u crc16_x25=%04x\n",
                   snapshot_count, (unsigned)logger.snapshot_length,
                   crc16_x25(logger.snapshot, logger.snapshot_length));
            dump_offset = 0;
            dumping = true;
        }
    }
    if (dumping) {
        if (dump_offset < logger.snapshot_length) {
            size_t end = dump_offset + 32u;
            if (end > logger.snapshot_length) end = logger.snapshot_length;
            /* One short line per main-loop iteration preserves UART service. */
            char line[110];
            int used = snprintf(line, sizeof(line), "[BINARY] offset=%04x hex=", (unsigned)dump_offset);
            for (size_t i = dump_offset; i < end; ++i)
                used += snprintf(line + used, sizeof(line) - (size_t)used, "%02x", logger.snapshot[i]);
            printf("%s\n", line);
            dump_offset = end;
        } else {
            printf("[BINARY END] completion=%" PRIu32 "\n", snapshot_count);
            dumping = false;
        }
    }
}

/* Take one reading when due. CONFIG sets the interval in seconds; this function
 * schedules the next reading, so GP20 does not need to be pressed each time. */
static void sample_poll(void)
{
    uint64_t now = now_ms(NULL);
    if (!logger.running || now < next_sample_ms) return;
    int16_t temperature = 0;
    last_read_status = aht10_read_temp(i2c0, &temperature);
    /* Keep a failed read separate from a real zero-degree temperature. */
    bool ok = last_read_status == AHT10_OK;
    bool stored = live_logger_append(&logger, ok, temperature);
    next_sample_ms = now + (uint64_t)logger.interval_seconds * 1000u;
    if (ok) { last_temperature = temperature; have_reading = true; }
    if (!stdio_usb_connected()) return;
    if (ok) {
        int magnitude = temperature < 0 ? -temperature : temperature;
        printf("[SAMPLE] %s%d.%02d C; stored=%u total=%" PRIu32 "\n",
               temperature < 0 ? "-" : "", magnitude / 100, magnitude % 100,
               stored ? 1u : 0u, logger.samples);
    } else {
        printf("[SAMPLE] failed: %s; no invented temperature value\n", aht10_status_str(last_read_status));
    }
    if (logger.full) puts("[LOG] RAM capacity reached; sampling stopped; retrieve or clear");
}

int main(void)
{
    stdio_init_all();
    sleep_ms(250); /* Bounded settling only: UART never waits for a USB monitor. */
    live_logger_init(&logger);
    /* These callbacks connect our RAM logger to the existing Sensor protocol.
     * UART framing/reliability stays in the library, storage stays in live_logger. */
    static const pico1_backend_t backend = {
        .context = &logger, .configure = configure, .snapshot = snapshot,
        .clear_begin = clear_log, .release = release_log
    };
    protocol_link_t link = {.session_id = SESSION_ID, .log = protocol_message,
                           .write = send_bytes, .now_ms = now_ms};
    if (!pico1_init(&endpoint, link, NULL, 0, (pico1_hooks_t){.backend = &backend})) return 1;
    stream_parser_init(&parser);
    /* Enable UART RX before sensor initialization, so incoming bytes can be
     * saved even while the AHT10 driver is waiting for an I2C operation. */
    uart_init(SENSOR_UART, 115200);
    gpio_set_function(SENSOR_TX, GPIO_FUNC_UART);
    gpio_set_function(SENSOR_RX, GPIO_FUNC_UART);
    gpio_pull_up(SENSOR_RX); /* Idle HIGH even with the other board disconnected. */
    uart_set_hw_flow(SENSOR_UART, false, false);
    uart_set_format(SENSOR_UART, 8, 1, UART_PARITY_NONE);
    irq_set_exclusive_handler(UART1_IRQ, uart_rx_irq);
    irq_set_enabled(UART1_IRQ, true);
    uart_set_irq_enables(SENSOR_UART, true, false);
    i2c_init(i2c0, 100000);
    gpio_set_function(SENSOR_SDA, GPIO_FUNC_I2C);
    gpio_set_function(SENSOR_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(SENSOR_SDA);
    gpio_pull_up(SENSOR_SCL);
    init_status = aht10_init(i2c0);
    /* A failed initialization is reported, but later scheduled reads are still
     * attempted. Their own results determine whether a reading can be stored. */
    uint32_t observed_drops = 0;
    for (;;) {
        /* Missing bytes make the current frame unreliable. Reset parsing and
         * release any active transfer rather than report partial data as complete. */
        if (observed_drops != rx_dropped) {
            observed_drops = rx_dropped;
            stream_parser_init(&parser);
            pico1_abort_transfer(&endpoint);
            if (stdio_usb_connected()) printf("[UART] RX overflow: dropped=%" PRIu32 "; transfer aborted\n", observed_drops);
        }
        /* The interrupt only collects bytes. Decode/validate complete frames
         * here, then let the library handle the accepted command or ACK. */
        uint8_t byte;
        for (unsigned n = 0; n < RX_POLL_BUDGET && pop_byte(&byte); ++n) {
            if (!stream_parser_feed(&parser, byte)) continue;
            if (!frame_decode(parser.buffer, parser.length, &frame)) { ++rejected_frames; continue; }
            ++frames_received;
            pico1_handle_frame(&endpoint, &frame);
        }
        /* Keep retry timers, sampling and USB responsive on every pass. */
        pico1_tick(&endpoint);
        sample_poll();
        console_poll();
        sleep_ms(1);
    }
}
