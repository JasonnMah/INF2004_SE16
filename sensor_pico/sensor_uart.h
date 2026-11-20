/* Sensor-board hardware connection to the reusable UART protocol.
 * The shared library in lib/uart_protocol handles framing, ACKs and retries. */
#ifndef SENSOR_UART_H
#define SENSOR_UART_H
#include "pico1/pico1_protocol.h"

bool sensor_uart_init(const pico1_backend_t *backend);
#ifdef SENSOR_BATCHED
#include "batch/batch_sensor.h"
bool sensor_uart_init_batched(const pico1_backend_t *backend, batch_source_t source);
#endif
#ifdef SENSOR_LIGHT_SLEEP
void sensor_uart_enable_wake(void);
bool sensor_uart_can_sleep(void);
void sensor_uart_woke(void);
#endif
void sensor_uart_poll(void);
void sensor_uart_report(void);
#endif
