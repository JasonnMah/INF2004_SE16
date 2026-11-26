/*
 * low_power.h - light sleep that wakes on a timer alarm, button presses, or the UART RX line.
 *
 * Needs only the Pico SDK itself (written against 1.5.1; no pico-extras). It does NOT switch the
 * system clock to the crystal: it only gates peripheral clocks while the core is in deep sleep, so
 * the I2C baud rate and the USB serial are left alone.
 *
 * Wake sources
 *   timer        lp_arm_timer(when)        absolute time, so a periodic schedule does not drift
 *   buttons      lp_add_button(pin)        falling edge, debounced; each pin is reported separately
 *   UART RX      lp_add_uart_wake(...)     falling edge on the RX pin (the start bit of the first byte)
 *
 * The UART clocks are gated while asleep, so the byte that wakes the chip is lost (or garbled), and the
 * UART may flag a framing error. The Wi-Fi Pico sends 32 bytes of 0x55 immediately before
 * the frame. The Sensor looks for the remainder of that prefix and the start-of-frame delimiter.
 *
 * Usage
 *   lp_init();                          claim an alarm
 *   lp_add_button(PIN);                 any number of buttons, up to LP_MAX_BUTTONS (pull-up, to GND)
 *   lp_add_uart_wake(uart1, RX_PIN);    after the UART and its pin are set up
 *   lp_arm_timer(when);
 *   lp_sleep_until_event(work_pending); may return on any interrupt: check the events afterwards
 *   if (lp_take_timer()) ...   if (lp_take_button(PIN)) ...   if (lp_take_uart_wake()) ...
 */
#ifndef LOW_POWER_H
#define LOW_POWER_H

#include <stdbool.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"

#define LP_MAX_BUTTONS 4

/* Claims one hardware alarm and installs the GPIO interrupt callback. */
void lp_init(void);

/* Button on `pin` to GND (internal pull-up). Returns false if the table is full. */
bool lp_add_button(uint pin);

/* Wake the chip when `rx_pin` (the RX pin of `uart`, idle high) goes low. Also makes
 * lp_sleep_until_event() wait for the UART transmitter to finish before the clocks stop. */
void lp_add_uart_wake(uart_inst_t *uart, uint rx_pin);

/* Schedule the timer event for absolute time `when` (replaces any earlier schedule). If `when` has
 * already passed, the event is raised immediately. */
void lp_arm_timer(absolute_time_t when);
void lp_cancel_timer(void);

/* Return true once per event, and clear it. Safe to call from the main loop at any time. */
bool lp_take_timer(void);
bool lp_take_button(uint pin);
bool lp_take_uart_wake(void);

/* Check work_pending with interrupts masked and UART wake armed, just before
 * gating clocks. The callback must only inspect state. Return true if WFI ran. */
bool lp_sleep_until_event(bool (*work_pending)(void));

#endif
