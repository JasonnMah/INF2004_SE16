#include "app.h"
#include "buttons.h"
#if APP_BATCHED
#include "flash_port.h"
#endif
void ui_run(void)
{
    app_button_t buttons[3] = {{0}};
    const unsigned pins[3] = {APP_CONFIG, APP_RETRIEVE, APP_CLEAR};
    rt_buttons_init();
#if APP_BATCHED
    unsigned long long clear_since = 0;
    unsigned clear_sent = 0;
#endif
    unsigned reports = 0;
    unsigned long long report = 0;
    for (;;) {
#if APP_BATCHED
        flash_port_core1_poll(); /* Outside kernel calls/locks, from physical core 1. */
#endif
        unsigned long long now = rt_now_ms();
        unsigned selected = 0;
        /* Sample every button even while busy: ignored holds never queue a
         * delayed command. Simultaneous presses use GP20, GP21, GP22 order. */
        for (unsigned i = 0; i < 3; ++i) {
            unsigned pressed = app_button_event(&buttons[i], rt_button_low(pins[i]), now);
#if APP_BATCHED
            if (pins[i] == APP_CLEAR) {
                if (!rt_button_low(APP_CLEAR)) { clear_since = 0; clear_sent = 0; }
                else if (pressed) clear_since = now;
                pressed = clear_since && !clear_sent && now - clear_since >= 2000;
                if (pressed) clear_sent = 1;
            }
#endif
            if (pressed) {
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
