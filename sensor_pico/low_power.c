#include "low_power.h"

/* JZ's RP2040 clock-gated sleep. Application-specific work stays in main.c. */

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/timer.h"
#include "hardware/regs/clocks.h"
#include "hardware/regs/m0plus.h"
#include "hardware/structs/scb.h"

#define DEBOUNCE_US  50000u            /* ignore edges within 50 ms of the last accepted one */

typedef struct {
    uint          pin;
    uint32_t      last_us;
    volatile bool flag;
} lp_button_t;

static lp_button_t   buttons[LP_MAX_BUTTONS];
static unsigned      n_buttons;
static volatile bool timer_flag;
static volatile bool uart_flag;
static int           alarm_num = -1;
static uart_inst_t  *wake_uart;
static uint          wake_pin;

/* ---------------------------------------------------------------- interrupt handlers */

static void on_alarm(uint id) {
    (void)id;
    timer_flag = true;
}

static void on_gpio(uint gpio, uint32_t events) {
    (void)events;
    for (unsigned i = 0; i < n_buttons; i++) {
        if (buttons[i].pin != gpio) continue;
        uint32_t now = time_us_32();
        if ((uint32_t)(now - buttons[i].last_us) < DEBOUNCE_US) return;
        buttons[i].last_us = now;
        buttons[i].flag = true;
        return;
    }
}

/* ---------------------------------------------------------------- setup */

static void install_gpio_callback(void) {
    gpio_set_irq_callback(on_gpio);
    irq_set_enabled(IO_IRQ_BANK0, true);
}

void lp_init(void) {
    alarm_num = hardware_alarm_claim_unused(true);
    hardware_alarm_set_callback((uint)alarm_num, on_alarm);
    install_gpio_callback();
}

bool lp_add_button(uint pin) {
    if (n_buttons >= LP_MAX_BUTTONS) return false;
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_pull_up(pin);
    buttons[n_buttons].pin = pin;
    buttons[n_buttons].last_us = time_us_32() - DEBOUNCE_US;
    buttons[n_buttons].flag = false;
    n_buttons++;
    gpio_set_irq_enabled(pin, GPIO_IRQ_EDGE_FALL, true);
    return true;
}

void lp_add_uart_wake(uart_inst_t *uart, uint rx_pin) {
    wake_uart = uart;
    wake_pin = rx_pin;
    /* The edge interrupt is armed only just before sleeping (see lp_sleep_until_event): while the chip
     * is awake the UART has its own interrupt, and an edge interrupt per byte would only add load. */
}

void lp_arm_timer(absolute_time_t when) {
    uint32_t s = save_and_disable_interrupts();
    timer_flag = false;
    if (hardware_alarm_set_target((uint)alarm_num, when)) {
        timer_flag = true;             /* target already passed: due now */
    }
    restore_interrupts(s);
}

void lp_cancel_timer(void) {
    uint32_t s = save_and_disable_interrupts();
    hardware_alarm_cancel((uint)alarm_num);
    timer_flag = false;
    restore_interrupts(s);
}

bool lp_take_timer(void) {
    uint32_t s = save_and_disable_interrupts();
    bool f = timer_flag;
    timer_flag = false;
    restore_interrupts(s);
    return f;
}

bool lp_take_button(uint pin) {
    bool f = false;
    uint32_t s = save_and_disable_interrupts();
    for (unsigned i = 0; i < n_buttons; i++) {
        if (buttons[i].pin == pin) {
            f = buttons[i].flag;
            buttons[i].flag = false;
            break;
        }
    }
    restore_interrupts(s);
    return f;
}

bool lp_take_uart_wake(void) {
    uint32_t s = save_and_disable_interrupts();
    bool f = uart_flag;
    uart_flag = false;
    restore_interrupts(s);
    return f;
}

/* ---------------------------------------------------------------- sleep */

static bool any_button_flag(void) {
    for (unsigned i = 0; i < n_buttons; i++) if (buttons[i].flag) return true;
    return false;
}

/* Clocks that stay on while the core sleeps. Everything else is gated. */
static void gate_clocks_for_sleep(void) {
    uint32_t en0 = CLOCKS_SLEEP_EN0_CLK_SYS_IO_BITS      /* GPIO edge detection (buttons, UART RX pin) */
                 | CLOCKS_SLEEP_EN0_CLK_SYS_PADS_BITS;   /* precaution: pad logic. Try removing it to save power. */
    uint32_t en1 = CLOCKS_SLEEP_EN1_CLK_SYS_TIMER_BITS;  /* the alarm */

#if LIB_PICO_STDIO_USB
    en1 |= CLOCKS_SLEEP_EN1_CLK_SYS_USBCTRL_BITS         /* keep the USB serial connection alive */
         | CLOCKS_SLEEP_EN1_CLK_USB_USBCTRL_BITS;
#endif
#if LIB_PICO_STDIO_UART
#if PICO_DEFAULT_UART
    en1 |= CLOCKS_SLEEP_EN1_CLK_PERI_UART1_BITS | CLOCKS_SLEEP_EN1_CLK_SYS_UART1_BITS;
#else
    en1 |= CLOCKS_SLEEP_EN1_CLK_PERI_UART0_BITS | CLOCKS_SLEEP_EN1_CLK_SYS_UART0_BITS;
#endif
#endif

    clocks_hw->sleep_en0 = en0;
    clocks_hw->sleep_en1 = en1;
}

bool lp_sleep_until_event(bool (*work_pending)(void)) {
    if (wake_uart) uart_tx_wait_blocking(wake_uart);   /* the last byte must leave the shifter first */

    /* Interrupts are masked while we decide to sleep. __wfi() still wakes on a pending interrupt even
     * with PRIMASK set, so an edge that arrives just before the __wfi() is not lost. The handlers run
     * when interrupts are restored below. */
    uint32_t s = save_and_disable_interrupts();

    if (wake_uart) {
        gpio_acknowledge_irq(wake_pin, GPIO_IRQ_EDGE_FALL);          /* forget edges from earlier bytes */
        gpio_set_irq_enabled(wake_pin, GPIO_IRQ_EDGE_FALL, true);
        if (!gpio_get(wake_pin)) uart_flag = true;                   /* a byte is already arriving: stay awake */
    }

    bool slept = !timer_flag && !any_button_flag() && !uart_flag &&
                 (!work_pending || !work_pending());
    if (slept) {
        uint32_t sleep_en0 = clocks_hw->sleep_en0;
        uint32_t sleep_en1 = clocks_hw->sleep_en1;
        uint32_t scr = scb_hw->scr;
        gate_clocks_for_sleep();
        scb_hw->scr = scr | M0PLUS_SCR_SLEEPDEEP_BITS;
        __dsb();
        __wfi();
        scb_hw->scr = scr;
        clocks_hw->sleep_en0 = sleep_en0;
        clocks_hw->sleep_en1 = sleep_en1;
    }

    if (wake_uart) {
        /* Capture the latched edge before disabling it. UART1's IRQ handles
         * the buffered bytes once interrupts are restored. */
        if (slept && (gpio_get_irq_event_mask(wake_pin) & GPIO_IRQ_EDGE_FALL))
            uart_flag = true;
        gpio_set_irq_enabled(wake_pin, GPIO_IRQ_EDGE_FALL, false);
        gpio_acknowledge_irq(wake_pin, GPIO_IRQ_EDGE_FALL);
    }
    restore_interrupts(s);
    return slept;
}
