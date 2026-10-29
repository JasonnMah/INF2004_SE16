#include "app.h"
#include "buttons.h"
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
