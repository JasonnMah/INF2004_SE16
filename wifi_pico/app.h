#ifndef WIFI_UART_APP_H
#define WIFI_UART_APP_H
#include "protocol/storage_profile.h"
/* Kernel/standard-C boundary: no kernel headers or size_t cross this header. */
#define APP_CONFIG 20u
#define APP_RETRIEVE 21u
#define APP_CLEAR 22u
#define APP_STATUS_CONFIG 1u
#define APP_STATUS_RETRIEVE 2u
#define APP_STATUS_CLEAR 3u
#define APP_STATUS_FAILED 4u
#define APP_STATUS_UPLOAD 5u
#define APP_STATUS_TRANSFERRING 6u
#define APP_STATUS_LOADING 7u
/* Combined image only: transport prefix, outside the unchanged protocol frame.
 * Duration is provisional until JZ's actual sleep/wake setup is available. */
#define APP_UART_PREAMBLE_BYTE 0x55u
#define APP_UART_PREAMBLE_LENGTH 32u
#define APP_TRANSFER_BLINK_MS 100u
#define APP_LOADING_BLINK_MS 400u
#define APP_TRANSFER_HEARTBEAT_MS 250u
/* Expiry prevents a lost result indication leaving GP6 blinking forever. */
#define APP_TRANSFER_LEASE_MS 1000u
#ifndef APP_COMBINED
#define APP_COMBINED 0
#endif
#define APP_QUEUE_DEPTH 8u
#define APP_RX_CAPACITY 2048u
#define APP_RECEIVE_CAPACITY STORAGE_TRANSFER_CAPACITY
#ifndef APP_SENSOR_EMULATOR
#define APP_SENSOR_EMULATOR 1
#endif
#ifndef UART_FAULT
#define UART_FAULT 0
#endif
/* Normal use reports each core once; lab builds can enable periodic counters. */
#ifndef APP_VERBOSE_DIAGNOSTICS
#define APP_VERBOSE_DIAGNOSTICS 0
#endif
#define APP_FAULT_INJECTION (APP_SENSOR_EMULATOR || UART_FAULT != 0)
typedef struct {
    unsigned kind;
    unsigned arg;
    unsigned long long at_ms;
} app_message_t;
typedef struct {
    unsigned irq_count, bytes_seen, dropped, faults, notify_errors;
} app_rx_stats_t;

unsigned long long rt_now_ms(void);
void rt_sleep(unsigned milliseconds);
void rt_log(const char *text);
void rt_numbers(const char *format, unsigned a, unsigned b, unsigned c);
void rt_trace(const char *name, unsigned loops, unsigned uart_unit);
int rt_setup(void);
int rt_uart_open(unsigned unit);
void rt_uart_write(unsigned unit, const unsigned char *bytes, unsigned length);
unsigned rt_rx_take(unsigned unit, unsigned char *bytes, unsigned capacity, unsigned *fault);
void rt_wait(unsigned unit, unsigned timeout_ms);
int rt_command_send(const app_message_t *message);
int rt_command_take(app_message_t *message);
int rt_status_send(const app_message_t *message);
int rt_status_take(app_message_t *message);
void rt_buttons_init(void);
unsigned rt_button_low(unsigned pin);
void rt_led_init(void);
void rt_led_set(unsigned on);
void rt_buzzer_init(void);
void rt_buzzer_set(unsigned on);
void rt_fatal(const char *operation, int error);

void communication_init(void);
void communication_step(void);
void communication_run(void);
void sensor_emulator_init(void);
void sensor_emulator_step(void);
void sensor_emulator_run(void);
void ui_run(void);
void status_init(void);
void status_step(void);
void status_run(void);

/* Owner-context snapshots for host tests; no cross-core buffer sharing. */
typedef struct {
    unsigned state, valid, length, completions, configured;
} communication_snapshot_t;
communication_snapshot_t communication_snapshot(void);
#endif
