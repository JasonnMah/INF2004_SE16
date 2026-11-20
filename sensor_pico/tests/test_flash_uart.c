/* Host integration test of the actual Sensor main, flash code and UART module.
 * SDK operations below are modeled; passing is not physical flash evidence. */
#define _GNU_SOURCE
#include <assert.h>
#include <stdlib.h>
#include <sys/mman.h>
#define main sensor_firmware_main
#include "../main.c"
#undef main
#include "hardware/uart.h"
#include "hardware/irq.h"
#include "pico2/pico2_protocol.h"
#include "protocol/stream_parser.h"

static uint64_t clock_us;
static bool pins[30], sensor_ok = true, corrupt_erase, fail_program;
static int16_t measured_temperature = 2500;
static unsigned programs, erases, completions, end_acks;
static unsigned uart_last_type;
static bool lose_end_ack, lose_data_acks, corrupt_config;
static void (*rx_handler)(void);
static uart_hw_t uart_hw;
static uint8_t incoming_byte;
static bool incoming_ready;
static pico2_ctx_t wifi;
static stream_parser_t wifi_parser;
static uint8_t wifi_bytes[STORAGE_TRANSFER_CAPACITY];
i2c_inst_t i2c_instance;
i2c_inst_t *i2c0 = &i2c_instance;
uart_inst_t uart_instance;
uart_inst_t *uart1 = &uart_instance;

uint64_t time_us_64(void) { return clock_us; }
void sleep_ms(uint32_t ms) { clock_us += (uint64_t)ms * 1000u; }
bool stdio_init_all(void) { return true; }
bool stdio_usb_connected(void) { return false; }
int getchar_timeout_us(uint32_t us) { (void)us; return PICO_ERROR_TIMEOUT; }
void tight_loop_contents(void) {}
void gpio_init(uint pin) { (void)pin; }
void gpio_set_dir(uint pin, bool output) { (void)pin; (void)output; }
void gpio_set_function(uint pin, uint function) { (void)pin; (void)function; }
void gpio_pull_up(uint pin) { pins[pin] = true; }
bool gpio_get(uint pin) { return pins[pin]; }
void i2c_init(i2c_inst_t *i2c, uint32_t baud) { (void)i2c; (void)baud; }
uint32_t save_and_disable_interrupts(void) { return 0; }
void restore_interrupts(uint32_t saved) { (void)saved; }
uint32_t uart_init(uart_inst_t *uart, uint32_t baud) { (void)uart; return baud; }
void uart_set_hw_flow(uart_inst_t *uart, bool cts, bool rts) { (void)uart; (void)cts; (void)rts; }
void uart_set_format(uart_inst_t *uart, uint bits, uint stop, uint parity) {
    (void)uart; assert(bits == 8 && stop == 1 && parity == UART_PARITY_NONE);
}
void uart_set_irq_enables(uart_inst_t *uart, bool rx, bool tx) { (void)uart; assert(rx && !tx); }
void irq_set_exclusive_handler(uint irq, void (*handler)(void)) { assert(irq == UART1_IRQ); rx_handler = handler; }
void irq_set_enabled(uint irq, bool enabled) { (void)irq; (void)enabled; }
bool uart_is_readable(uart_inst_t *uart) { (void)uart; return incoming_ready; }
uart_hw_t *uart_get_hw(uart_inst_t *uart) {
    (void)uart; incoming_ready = false; uart_hw.dr = incoming_byte; return &uart_hw;
}
static void feed_sensor(uint8_t byte) {
    incoming_byte = byte; incoming_ready = true; rx_handler();
}
static uint64_t peer_time(void *context) { (void)context; return clock_us / 1000u; }

static void peer_write(void *context, const uint8_t *bytes, size_t length) {
    (void)context;
    protocol_frame_t sent;
    assert(frame_decode(bytes, length, &sent));
    if (sent.message_type == MSG_ACK) {
        if (uart_last_type == MSG_DATA && lose_data_acks) return;
        if (uart_last_type == MSG_TRANSFER_END) {
            ++end_acks;
            if (lose_end_ack) { lose_end_ack = false; return; }
        }
    }
    /* The real Wi-Fi firmware adds the preamble outside its frame. */
    for (unsigned i = 0; i < 32; ++i) feed_sensor(0x55);
    for (size_t i = 0; i < length; ++i)
        feed_sensor(corrupt_config && i == 3 ? (uint8_t)(bytes[i] ^ 1u) : bytes[i]);
    corrupt_config = false;
}

void uart_write_blocking(uart_inst_t *uart, const uint8_t *bytes, size_t length) {
    (void)uart;
    protocol_frame_t sent;
    assert(frame_decode(bytes, length, &sent));
    uart_last_type = sent.message_type;
    for (size_t i = 0; i < length; ++i) {
        if (!stream_parser_feed(&wifi_parser, bytes[i])) continue;
        protocol_frame_t frame;
        assert(frame_decode(wifi_parser.buffer, wifi_parser.length, &frame));
        pico2_handle_frame(&wifi, &frame);
    }
}

void flash_range_program(uint32_t offset, const uint8_t *data, size_t length) {
    assert(length == FLASH_PAGE_SIZE);
    ++programs;
    uint8_t *flash = (uint8_t *)(uintptr_t)(XIP_BASE + offset);
    if (fail_program) return;
    for (size_t i = 0; i < length; ++i) flash[i] &= data[i];
}
void flash_range_erase(uint32_t offset, size_t length) {
    assert(offset >= PICO_FLASH_SIZE_BYTES - FLOG_REGION_BYTES);
    assert(length == FLASH_SECTOR_SIZE);
    ++erases;
    uint8_t *flash = (uint8_t *)(uintptr_t)(XIP_BASE + offset);
    memset(flash, 0xFF, length);
    if (corrupt_erase) flash[0] = 0; /* SDK returns success, read-back must fail. */
}
int flash_safe_execute(void (*function)(void *), void *arg, uint32_t timeout) {
    (void)timeout; function(arg); clock_us += 1000u; return PICO_OK;
}
aht10_status_t aht10_init(i2c_inst_t *i2c) { (void)i2c; return AHT10_OK; }
aht10_status_t aht10_read_temp(i2c_inst_t *i2c, int16_t *temperature) {
    (void)i2c;
    if (!sensor_ok) return AHT10_ERR_I2C;
    *temperature = measured_temperature;
    return AHT10_OK;
}
const char *aht10_status_str(aht10_status_t status) { return status == AHT10_OK ? "ok" : "I2C error"; }

static void complete(pico2_ctx_t *peer) { assert(peer->received_log_valid); ++completions; }
static const pico1_backend_t storage = {
    .configure = uart_configure, .snapshot = uart_snapshot,
    .clear_begin = uart_clear_begin, .clear_poll = uart_clear_poll,
    .release = uart_release
};

static void restart_application(void) {
    enc_reset_page(&enc);
    need_start = true;
    log_stopped = storage_failed = sampling = configured = boot_written = false;
    snapshot_held = clear_active = dumping = false;
    snapshot_length = samples_saved = clear_sectors_left = 0;
    config_timestamp = FAKE_UNIX_TIME;
    config_interval_seconds = FAKE_INTERVAL * 60u;
    config_started_ms = next_sample_ms = 0;
    flog_status_t result = flog_init();
    assert(result == FLOG_OK || result == FLOG_ERR_FULL);
    storage_ready = true;
    log_stopped = result == FLOG_ERR_FULL;
    assert(sensor_uart_init(&storage));
    stream_parser_init(&wifi_parser);
    assert(pico2_init(&wifi, (protocol_link_t){.session_id = 0x11223344,
        .write = peer_write, .now_ms = peer_time}, wifi_bytes, sizeof(wifi_bytes),
        1700000000, 60, (pico2_hooks_t){.transfer_complete = complete}));
    wifi.state = P2_COMPLETE;
    wifi.clear_ack_timeout_ms = STORAGE_CLEAR_ACK_TIMEOUT_MS;
    lose_end_ack = lose_data_acks = corrupt_config = false;
    completions = end_acks = 0;
}

static void pump(unsigned milliseconds) {
    for (unsigned i = 0; i < milliseconds; i += 5) {
        sensor_uart_poll();
        pico2_tick(&wifi);
        clock_us += 5000u;
    }
}

static void repeat_command(uint8_t type) {
    uint8_t bytes[MAX_STUFFED_FRAME_SIZE];
    uint8_t payload[8];
    size_t n = 0;
    if (type == MSG_CONFIG) {
        write_u32_le(payload, wifi.config_timestamp);
        write_u32_le(payload + 4, wifi.config_interval);
        n = 8;
    }
    size_t length = frame_encode(type, 0, 0x11223344, wifi.pending_command.sequence,
                                payload, (uint16_t)n, bytes, sizeof(bytes));
    assert(length);
    peer_write(NULL, bytes, length);
}

#ifndef STORAGE_TEST_MAIN
#define STORAGE_TEST_MAIN main
#endif
int STORAGE_TEST_MAIN(int argc, char **argv) {
    assert(argc == 2);
    void *mapped = mmap((void *)(uintptr_t)XIP_BASE, PICO_FLASH_SIZE_BYTES,
                       PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    assert(mapped == (void *)(uintptr_t)XIP_BASE);
    memset(mapped, 0xFF, PICO_FLASH_SIZE_BYTES);
    restart_application();

    /* Local pin meanings and 30 ms press/release debounce remain unchanged. */
    assert(PIN_SAMPLE == 20 && PIN_FLUSH == 21 && PIN_DUMP == 22);
    button_t button;
    button_init(&button, PIN_SAMPLE);
    pins[PIN_SAMPLE] = false;
    for (unsigned i = 1; i < DEBOUNCE_TICKS; ++i) assert(!button_pressed(&button));
    assert(button_pressed(&button));
    for (unsigned i = 0; i < 20; ++i) assert(!button_pressed(&button));
    pins[PIN_SAMPLE] = true;
    for (unsigned i = 0; i < DEBOUNCE_TICKS; ++i) assert(!button_pressed(&button));
    pins[PIN_SAMPLE] = false;
    for (unsigned i = 1; i < DEBOUNCE_TICKS; ++i) assert(!button_pressed(&button));
    assert(button_pressed(&button));
    do_sample();
    assert(samples_saved == 1 && enc_has_unsealed(&enc));
    do_flush();
    assert(programs == 1 && !flog_page_is_empty(0));
    do_dump(); /* Same FLASHDUMP output and no deletion of saved readings. */
    assert(!flog_page_is_empty(0) && samples_saved == 1);
    assert(flog_erase_all());
    restart_application();

    /* CONFIG and automatic interval scheduling, using actual main.c callbacks. */
    assert(pico2_send_config(&wifi)); pump(20);
    assert(wifi.state == P2_COMPLETE && sampling && config_interval_seconds == 60);
    uint64_t configured_at = config_started_ms;
    repeat_command(MSG_CONFIG); pump(20);
    assert(config_started_ms == configured_at);
    sampling_poll(); assert(samples_saved == 1);
    clock_us += 59000000u; sampling_poll(); assert(samples_saved == 1);
    clock_us += 1000000u; measured_temperature = 2625;
    sampling_poll(); assert(samples_saved == 2);

    /* A retrieval automatically saves RAM to flash, then borrows a RAM copy of
     * the saved pages. New samples/flushes cannot alter that transfer copy. */
    lose_end_ack = true;
    assert(pico2_request_data(&wifi)); pump(200);
    assert(snapshot_held && completions == 1 && wifi.received_log_length == 256);
    uint8_t first[256]; memcpy(first, wifi_bytes, sizeof(first));
    uint8_t saved_flash[256]; flog_read_page(0, saved_flash);
    assert(!memcmp(first, saved_flash, sizeof(first)));
    FILE *fixture = fopen(argv[1], "wb"); assert(fixture);
    assert(fwrite(first, 1, sizeof(first), fixture) == sizeof(first)); assert(!fclose(fixture));
    measured_temperature = 2800; do_sample(); do_flush();
    assert(!memcmp(first, snapshot_bytes, sizeof(first)));
    pump(1200);
    assert(!snapshot_held && completions == 1 && end_acks == 2);

    /* Restart RAM/protocol state while retaining flash, as after a power cycle. */
    flog_read_page(0, saved_flash);
    restart_application();
    assert(!sampling && !enc.nib);
    assert(pico2_request_data(&wifi)); pump(1000);
    assert(wifi.received_log_valid && !memcmp(saved_flash, wifi_bytes, sizeof(saved_flash)));

    /* CRC rejection, bounded retries and fresh-command recovery. */
    corrupt_config = true;
    assert(pico2_send_config(&wifi)); pump(30);
    assert(wifi.pending_command.active && !sampling);
    pump(700); assert(sampling && wifi.state == P2_COMPLETE);
    uint64_t old_config = config_started_ms;
    wifi.config_interval = 61;
    assert(pico2_send_config(&wifi)); pump(100);
    assert(wifi.state == P2_FAILED && config_started_ms == old_config);
    wifi.config_interval = 60;
    lose_data_acks = true;
    assert(pico2_request_data(&wifi)); pump(4000);
    assert(!snapshot_held && !wifi.received_log_valid && wifi.state == P2_FAILED);
    lose_data_acks = false;
    assert(pico2_request_data(&wifi)); pump(1000);
    assert(wifi.received_log_valid && !snapshot_held);

    /* Overflow must abandon the borrowed copy and permit fresh retrieval. */
    lose_data_acks = true;
    assert(pico2_request_data(&wifi)); pump(20); assert(snapshot_held);
    for (unsigned i = 0; i < 1100; ++i) feed_sensor(0x55);
    pump(4000); assert(!snapshot_held && !wifi.received_log_valid);
    lose_data_acks = false;
    assert(pico2_request_data(&wifi)); pump(1000); assert(wifi.received_log_valid);

    /* No CLEAR ACK while sectors remain. Duplicate CLEAR does not restart it. */
    unsigned before = erases;
    assert(pico2_clear_log(&wifi)); pump(20);
    assert(clear_active && wifi.pending_command.active && erases > before);
    uint32_t remaining = clear_sectors_left;
    repeat_command(MSG_CLEAR_LOG); pump(10);
    assert(clear_sectors_left < remaining);
    pump(2000);
    assert(!clear_active && !sampling && !enc.nib && wifi.state == P2_COMPLETE);
    assert(erases - before == FLOG_REGION_BYTES / FLASH_SECTOR_SIZE);
    for (uint32_t page = 0; page < FLOG_PAGE_COUNT; ++page) assert(flog_page_is_empty(page));
    assert(pico2_request_data(&wifi)); pump(100);
    assert(wifi.received_log_valid && wifi.received_log_length == 0);

    /* A successful SDK call with non-erased bytes must never ACK CLEAR. */
    corrupt_erase = true;
    assert(pico2_clear_log(&wifi)); pump(100);
    assert(wifi.state == P2_FAILED && storage_failed && log_stopped);
    corrupt_erase = false;
    assert(pico2_clear_log(&wifi)); pump(2000);
    assert(wifi.state == P2_COMPLETE && !storage_failed && !log_stopped);

    /* A failed program cannot publish a partial saved log. */
    do_sample(); fail_program = true;
    assert(pico2_request_data(&wifi)); pump(100);
    assert(wifi.state == P2_FAILED && !wifi.received_log_valid && storage_failed);
    fail_program = false;
    assert(pico2_clear_log(&wifi)); pump(2000);
    assert(wifi.state == P2_COMPLETE);

    /* Reject more than four saved pages; never truncate to the Wi-Fi capacity. */
    uint8_t page[256]; memset(page, 0xFF, sizeof(page)); page[0] = 0x73;
    for (unsigned i = 0; i < 4; ++i) {
        assert(flog_program_page(page) == FLOG_OK); assert(flog_advance());
    }
    restart_application();
    assert(pico2_request_data(&wifi)); pump(2000);
    assert(wifi.received_log_valid && wifi.received_log_length == STORAGE_TRANSFER_CAPACITY);
    assert(flog_program_page(page) == FLOG_OK); assert(flog_advance());
    restart_application();
    assert(pico2_request_data(&wifi)); pump(100);
    assert(wifi.state == P2_FAILED && !wifi.received_log_valid && !snapshot_held);
    assert(!flog_page_is_empty(4));
    assert(!flog_erase_sector(FLOG_REGION_BYTES / FLASH_SECTOR_SIZE));
    corrupt_erase = true;
    assert(!flog_erase_all() && flog_is_full());
    corrupt_erase = false;
    assert(flog_erase_all());

    assert(!munmap(mapped, PICO_FLASH_SIZE_BYTES));
    puts("PASS Sensor flash UART host integration: original buttons/debounce, CONFIG, timed samples, real flash source, persistence model, immutable retries, lost final ACK, CRC, overflow, verified CLEAR/failures, capacity rejection");
    return 0;
}
