#include "sensor_uart.h"
#include <inttypes.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/uart.h"
#include "protocol/stream_parser.h"
#ifdef SENSOR_LIGHT_SLEEP
#include "low_power.h"
#endif

/* Sensor GP4 TX -> Wi-Fi GP1 RX; Sensor GP5 RX <- Wi-Fi GP0 TX. */
#define SENSOR_UART uart1
#define SENSOR_RX_PIN 5u
#define RX_CAPACITY 1024u
#define RX_POLL_BUDGET 128u
#define SESSION_ID 0x11223344u

#ifdef SENSOR_BATCHED
static batch_sensor_t batched;
#define endpoint batched.protocol
#else
static pico1_ctx_t endpoint;
#endif
static stream_parser_t parser;
static protocol_frame_t frame;
#ifdef SENSOR_LIGHT_SLEEP
/* Keep receive errors in order with good bytes, including the wake prefix.
 * A framing error during wake must not flush a valid frame behind it. */
typedef uint16_t rx_entry_t;
#define RX_BAD_BYTE 0x100u
#define WAKE_BYTE 0x55u
#define UART_LINGER_MS 500u
static uint64_t last_activity_ms;
static uint32_t last_head, uart_wakes;
static unsigned prefix_bytes;
static bool wake_frame_pending, seek_wake_prefix;
#else
typedef uint8_t rx_entry_t;
#endif
static rx_entry_t rx[RX_CAPACITY];
static volatile uint32_t head, tail, rx_errors, rx_dropped;
static uint32_t frames_received, rejected_frames, observed_errors, observed_drops;

static uint64_t now_ms(void *context) {
    (void)context;
    return time_us_64() / 1000u;
}

static void send_bytes(void *context, const uint8_t *bytes, size_t length) {
    (void)context;
    uart_write_blocking(SENSOR_UART, bytes, length);
}

static void log_message(const char *text) {
    if (stdio_usb_connected()) printf("%s", text);
}

/* Keep only byte collection in the interrupt. AHT10 conversion can block the
 * main loop briefly, so incoming UART bytes need their own bounded buffer. */
static void uart_rx_irq(void) {
    while (uart_is_readable(SENSOR_UART)) {
        uint32_t value = uart_get_hw(SENSOR_UART)->dr;
        if (value & 0xF00u) {
            ++rx_errors;
            uart_get_hw(SENSOR_UART)->rsr = 0;
#ifdef SENSOR_LIGHT_SLEEP
            value = RX_BAD_BYTE;
#else
            continue;
#endif
        }
        if (head - tail == RX_CAPACITY) {
            ++rx_dropped;
        } else {
            rx[head++ % RX_CAPACITY] = (rx_entry_t)value;
        }
    }
}

static bool pop_byte(rx_entry_t *byte) {
    /* Single-core bare-metal only: prevent the UART interrupt changing these
     * counters while main removes a byte. This is not an SMP lock. */
    uint32_t saved = save_and_disable_interrupts();
    bool available = head != tail;
    if (available) *byte = rx[tail++ % RX_CAPACITY];
    restore_interrupts(saved);
    return available;
}

bool sensor_uart_init(const pico1_backend_t *backend) {
    protocol_link_t link = {.session_id = SESSION_ID, .write = send_bytes,
                           .now_ms = now_ms, .log = log_message};
    if (!pico1_init(&endpoint, link, NULL, 0,
                    (pico1_hooks_t){.backend = backend})) return false;
    head = tail = rx_errors = rx_dropped = 0;
    frames_received = rejected_frames = observed_errors = observed_drops = 0;
#ifdef SENSOR_LIGHT_SLEEP
    last_activity_ms = now_ms(NULL);
    last_head = uart_wakes = prefix_bytes = 0;
    wake_frame_pending = seek_wake_prefix = false;
#endif
    stream_parser_init(&parser);
    uart_init(SENSOR_UART, 115200);
    gpio_set_function(4, GPIO_FUNC_UART);
    gpio_set_function(SENSOR_RX_PIN, GPIO_FUNC_UART);
    gpio_pull_up(SENSOR_RX_PIN);
    uart_set_hw_flow(SENSOR_UART, false, false);
    uart_set_format(SENSOR_UART, 8, 1, UART_PARITY_NONE);
    irq_set_exclusive_handler(UART1_IRQ, uart_rx_irq);
    irq_set_enabled(UART1_IRQ, true);
    uart_set_irq_enables(SENSOR_UART, true, false);
    return true;
}

#ifdef SENSOR_LIGHT_SLEEP
void sensor_uart_enable_wake(void) {
    lp_add_uart_wake(SENSOR_UART, SENSOR_RX_PIN);
}

bool sensor_uart_can_sleep(void) {
    return !batched.active && !endpoint.pending_transfer.active && !endpoint.clear_pending &&
           !uart_is_readable(SENSOR_UART) && head == tail && head == last_head &&
           now_ms(NULL) - last_activity_ms >= UART_LINGER_MS;
}

void sensor_uart_woke(void) {
    ++uart_wakes;
    last_activity_ms = now_ms(NULL);
    wake_frame_pending = seek_wake_prefix = true;
    prefix_bytes = 0;
    stream_parser_init(&parser);
}

static bool receive_after_wake(rx_entry_t byte) {
    if (byte == RX_BAD_BYTE) {
        stream_parser_init(&parser);
        prefix_bytes = 0;
        seek_wake_prefix = wake_frame_pending;
        return false;
    }
    if (!seek_wake_prefix) return true;
    /* The first wake byte may be corrupt. Require the tail of the preamble
     * before accepting a delimiter, then use the normal frame CRC. */
    if (byte == SOF && prefix_bytes >= 2) {
        seek_wake_prefix = false;
        return true;
    }
    if (byte != WAKE_BYTE) prefix_bytes = 0;
    else if (prefix_bytes < 2) ++prefix_bytes;
    return false;
}
#endif

void sensor_uart_poll(void) {
#ifdef SENSOR_LIGHT_SLEEP
    uint32_t received_head = head;
    if (last_head != received_head || observed_drops != rx_dropped) {
        last_head = received_head;
        last_activity_ms = now_ms(NULL);
    }
    if (observed_drops != rx_dropped) {
#else
    if (observed_errors != rx_errors || observed_drops != rx_dropped) {
#endif
        uint32_t saved = save_and_disable_interrupts();
        observed_errors = rx_errors;
        observed_drops = rx_dropped;
        tail = head; /* Discard fragments after a known missing/damaged byte. */
        restore_interrupts(saved);
        stream_parser_init(&parser);
#ifdef SENSOR_LIGHT_SLEEP
        seek_wake_prefix = wake_frame_pending;
        prefix_bytes = 0;
#endif
        pico1_abort_transfer(&endpoint);
        if (stdio_usb_connected())
            printf("[UART] RX error/overflow: errors=%" PRIu32 " dropped=%" PRIu32
                   "; transfer aborted; retry the command\n", observed_errors, observed_drops);
    }
    rx_entry_t byte;
    for (unsigned n = 0; n < RX_POLL_BUDGET && pop_byte(&byte); ++n) {
#ifdef SENSOR_LIGHT_SLEEP
        if (!receive_after_wake(byte)) continue;
#endif
        if (!stream_parser_feed(&parser, (uint8_t)byte)) continue;
        if (!frame_decode(parser.buffer, parser.length, &frame)) {
            ++rejected_frames;
#ifdef SENSOR_LIGHT_SLEEP
            seek_wake_prefix = wake_frame_pending;
            prefix_bytes = 0;
#endif
            continue;
        }
        ++frames_received;
#ifdef SENSOR_BATCHED
        batch_sensor_handle(&batched, &frame);
#else
        pico1_handle_frame(&endpoint, &frame);
#endif
#ifdef SENSOR_LIGHT_SLEEP
        if (wake_frame_pending) {
            wake_frame_pending = false;
            if (stdio_usb_connected())
                printf("[SLEEP] UART woke Sensor on GP5; frame CRC valid (type=0x%02x)\n",
                       (unsigned)frame.message_type);
        }
#endif
    }
    /* Also completes pending verified CLEAR operations. */
    pico1_tick(&endpoint);
}

void sensor_uart_report(void) {
    printf("[UART] UART1 GP4 TX / GP5 RX; 115200 8N1; session=11223344\n");
    printf("[UART] frames=%" PRIu32 " rejected=%" PRIu32 " errors=%" PRIu32
           " dropped=%" PRIu32 " transfer_active=%u clear_pending=%u\n",
           frames_received, rejected_frames, rx_errors, rx_dropped,
           endpoint.transfer_state != P1_TRANSFER_IDLE ? 1u : 0u,
           endpoint.clear_pending ? 1u : 0u);
#ifdef SENSOR_LIGHT_SLEEP
    printf("[SLEEP] UART wakes=%" PRIu32 "\n", uart_wakes);
#endif
}

#ifdef SENSOR_BATCHED
bool sensor_uart_init_batched(const pico1_backend_t *backend, batch_source_t source) {
    if (!sensor_uart_init(backend)) return false;
    protocol_link_t link = endpoint.link;
    return batch_sensor_init(&batched, link, backend, source);
}
#endif
