#include "app.h"
#include "ui_logic.h"
void ui_run(void)
{
    app_button_t buttons[3] = {{0}};
    const unsigned pins[3] = {APP_CONFIG, APP_RETRIEVE, APP_CLEAR};
    rt_buttons_init();
    unsigned reports = 0;
    unsigned long long report = 0;
    for (;;) {
        unsigned long long now = rt_now_ms();
        unsigned selected = 0;
        /* Sample every button even while busy: ignored holds never queue a
         * delayed command. Simultaneous presses use GP20, GP21, GP22 order. */
        for (unsigned i = 0; i < 3; ++i) {
            if (app_button_event(&buttons[i], rt_button_low(pins[i]), now)) {
                rt_numbers("[UI] GP%u pressed\n", pins[i], 0, 0);
                if (!selected) { selected = pins[i]; }
            }
        }
        if (selected) {
            app_message_t message = {.kind = selected, .at_ms = now};
            if (!rt_command_send(&message)) {
                rt_log("[UI] Command queue full; press ignored\n");
            }
        }
        if (now >= report) {
            rt_trace("UI", ++reports, 2);
            report = now + 5000u;
        }
        rt_sleep(10);
    }
}
void status_run(void)
{
    rt_led_init();
    unsigned remaining = 0, on_ms = 0, on = 0;
    unsigned long long deadline = 0;
    for (;;) {
        app_message_t message;
        unsigned long long now = rt_now_ms();
        if (rt_status_take(&message)) {
            remaining = app_flash_count(message.kind);
            on_ms = app_flash_on_ms(message.kind);
            on = 1;
            rt_led_set(on);
            deadline = now + on_ms;
        }
        if (remaining && now >= deadline) {
            if (on) {
                on = 0;
                --remaining;
                deadline = now + 100u;
            } else {
                on = 1;
                deadline = now + on_ms;
            }
            rt_led_set(on);
        }
        rt_sleep(10);
    }
}
