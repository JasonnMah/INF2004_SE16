/* Model bytes delivered after GP5 wakes the Sensor. On-board testing is
 * required for the actual clock transition and preamble timing. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "sensor_uart.h"

static uint64_t clock_ms;
static uart_inst_t uart_instance;
uart_inst_t *uart1 = &uart_instance;
static uart_hw_t registers;
static uint32_t incoming_word;
static bool incoming_ready;
static void (*rx_irq)(void);
static protocol_frame_t sent;
static unsigned writes, configurations, clear_checks;
static bool range_open;

uint64_t time_us_64(void) { return clock_ms * 1000; }
bool stdio_usb_connected(void) { return false; }
uint32_t save_and_disable_interrupts(void) { return 0; }
void restore_interrupts(uint32_t saved) { (void)saved; }
void gpio_set_function(uint pin, uint function) { (void)pin; (void)function; }
void gpio_pull_up(uint pin) { assert(pin == 5); }
void lp_add_uart_wake(uart_inst_t *uart, uint pin) { assert(uart == uart1 && pin == 5); }
uint32_t uart_init(uart_inst_t *uart, uint32_t baud) { assert(uart == uart1); return baud; }
void uart_set_hw_flow(uart_inst_t *uart, bool cts, bool rts) { (void)uart; assert(!cts && !rts); }
void uart_set_format(uart_inst_t *uart, uint bits, uint stop, uint parity) {
    (void)uart;
    assert(bits == 8 && stop == 1 && parity == UART_PARITY_NONE);
}
void irq_set_exclusive_handler(uint irq, void (*handler)(void)) { assert(irq == UART1_IRQ); rx_irq = handler; }
void irq_set_enabled(uint irq, bool enabled) { assert(irq == UART1_IRQ && enabled); }
void uart_set_irq_enables(uart_inst_t *uart, bool rx, bool tx) { (void)uart; assert(rx && !tx); }
bool uart_is_readable(uart_inst_t *uart) { (void)uart; return incoming_ready; }
uart_hw_t *uart_get_hw(uart_inst_t *uart) {
    (void)uart;
    registers.dr = incoming_word;
    incoming_ready = false;
    return &registers;
}
void uart_write_blocking(uart_inst_t *uart, const uint8_t *bytes, size_t length) {
    (void)uart;
    assert(frame_decode(bytes, length, &sent));
    ++writes;
}

static void feed(uint32_t word) {
    incoming_word = word;
    incoming_ready = true;
    rx_irq();
}

static void command(uint8_t type, uint32_t session, uint16_t sequence,
                    const uint8_t *payload, uint16_t length, bool damaged_wake) {
    uint8_t bytes[MAX_STUFFED_FRAME_SIZE];
    size_t used = frame_encode(type, 0, session, sequence, payload, length, bytes, sizeof(bytes));
    assert(used);
    if (damaged_wake) {
        feed(0x100u | 0x55u); /* UART flagged the first preamble byte. */
        feed(SOF);            /* Another garbled wake byte resembles a delimiter. */
    }
    for (unsigned i = damaged_wake ? 2 : 0; i < 32; ++i) feed(0x55);
    for (size_t i = 0; i < used; ++i) feed(bytes[i]);
}

static bool configure(void *context, uint32_t timestamp, uint32_t seconds) {
    (void)context;
    assert(timestamp == 1700000000u && seconds == 60);
    ++configurations;
    return true;
}

static bool open_range(void *context, const batch_info_t *request, batch_info_t *result,
                       uint8_t *buffer) {
    (void)context;
    *result = *request;
    result->sha256[0] = 1;
    result->total = result->length = 8;
    memset(buffer, 0x5A, 8);
    range_open = true;
    return true;
}

static bool finish_range(void *context, const uint8_t *digest, uint32_t total) {
    (void)context;
    assert(range_open && digest[0] == 1 && total == 8);
    range_open = false;
    return true;
}

static pico1_storage_result_t clear_begin(void *context) {
    (void)context;
    return P1_STORAGE_PENDING;
}
static pico1_storage_result_t clear_poll(void *context) {
    (void)context;
    return ++clear_checks == 3 ? P1_STORAGE_READY : P1_STORAGE_PENDING;
}

int main(void) {
    const uint32_t base = 0x11223344u, range_session = 0x12345678u;
    static const pico1_backend_t backend = {.configure = configure,
        .clear_begin = clear_begin, .clear_poll = clear_poll};
    assert(sensor_uart_init_batched(&backend,
        (batch_source_t){.open = open_range, .finish = finish_range}));
    sensor_uart_enable_wake();
    clock_ms = 501;
    assert(sensor_uart_can_sleep());

    /* The full CONFIG is already in the ring when the wake is serviced. */
    uint8_t config[8];
    write_u32_le(config, 1700000000u);
    write_u32_le(config + 4, 60);
    command(MSG_CONFIG, base, 1, config, sizeof(config), true);
    assert(!sensor_uart_can_sleep());
    sensor_uart_woke();
    sensor_uart_poll();
    assert(configurations == 1 && writes == 1 && sent.message_type == MSG_ACK);
    assert(read_u16_le(sent.payload) == 1 && !sensor_uart_can_sleep());

    clock_ms += 501;
    assert(sensor_uart_can_sleep());
    uint8_t open[UART_BATCH_OPEN_SIZE];
    batch_info_t request = {.session = range_session};
    batch_pack(&request, open, false);
    sensor_uart_woke();
    command(MSG_BATCH_OPEN, base, 2, open, sizeof(open), true);
    sensor_uart_poll();
    assert(range_open && sent.message_type == MSG_BATCH_INFO);

    const uint8_t first_block[2] = {0};
    command(MSG_REQUEST_DATA, range_session, 1, first_block, sizeof(first_block), false);
    sensor_uart_poll();
    assert(sent.message_type == MSG_DATA && sent.payload_length == 10);
    unsigned sent_at_boundary = writes;
    clock_ms += 5000;
    sensor_uart_poll();
    assert(writes == sent_at_boundary && !sensor_uart_can_sleep());

    uint8_t ack[2];
    write_u16_le(ack, sent.sequence);
    command(MSG_ACK, range_session, 2, ack, sizeof(ack), false);
    sensor_uart_poll();
    assert(sent.message_type == MSG_TRANSFER_END);
    write_u16_le(ack, sent.sequence);
    command(MSG_ACK, range_session, 3, ack, sizeof(ack), false);
    sensor_uart_poll();
    clock_ms += 501;
    assert(!sensor_uart_can_sleep()); /* A range ended, but the whole log is still held. */

    uint8_t done[UART_BATCH_DONE_SIZE] = {1};
    write_u32_le(done + 32, 8);
    command(MSG_BATCH_DONE, base, 3, done, sizeof(done), false);
    sensor_uart_poll();
    assert(!range_open && sent.message_type == MSG_ACK);
    clock_ms += 501;
    assert(sensor_uart_can_sleep());

    sensor_uart_woke();
    command(MSG_CLEAR_LOG, base, 4, NULL, 0, true);
    unsigned before_clear = writes;
    sensor_uart_poll();
    clock_ms += 501;
    assert(writes == before_clear && !sensor_uart_can_sleep());
    sensor_uart_poll();
    assert(writes == before_clear && !sensor_uart_can_sleep());
    sensor_uart_poll();
    assert(clear_checks == 3 && writes == before_clear + 1 && sent.message_type == MSG_ACK);
    assert(sensor_uart_can_sleep());
    puts("PASS Sensor wake-prefixed CONFIG, batch and CLEAR; queued frame survives framing error, transfer and erase block sleep");
    return 0;
}
