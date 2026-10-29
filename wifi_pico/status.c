#include "app.h"
#include "buttons.h"
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
