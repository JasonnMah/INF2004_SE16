/* Status Task alone owns GP6 and GP18; other tasks only send status events. */
#include "app.h"
#include "buttons.h"
static unsigned remaining, on_ms, on, failure, blink_ms;
static unsigned long long deadline, activity_until;

static void outputs(void)
{
    rt_led_set(on);
    rt_buzzer_set(failure && on); /* Three failure pulses; all other states silent. */
}
void status_init(void)
{
    remaining = on_ms = on = failure = blink_ms = 0;
    deadline = activity_until = 0;
    rt_led_init();
    rt_buzzer_init();
    outputs();
}
static void event(const app_message_t *message, unsigned long long now)
{
    if (message->kind == APP_STATUS_TRANSFERRING || message->kind == APP_STATUS_LOADING) {
        unsigned period = message->kind == APP_STATUS_LOADING ?
                          APP_LOADING_BLINK_MS : APP_TRANSFER_BLINK_MS;
        unsigned long long until = message->at_ms + APP_TRANSFER_LEASE_MS;
        if (until <= now) return; /* A delayed old event must not restart GP6. */
        unsigned blinking = activity_until > now && blink_ms == period;
        activity_until = until;
        if (blinking) return; /* Heartbeats extend activity without resetting phase. */
        blink_ms = period;
        failure = 0;
        remaining = 0;
        on = 1;
        deadline = now + blink_ms;
    } else {
        activity_until = 0;
        failure = message->kind == APP_STATUS_FAILED;
        remaining = app_flash_count(message->kind);
        on_ms = app_flash_on_ms(message->kind);
        on = 1;
        deadline = now + on_ms;
    }
    outputs();
}
void status_step(void)
{
    unsigned long long now = rt_now_ms();
    for (unsigned i = 0; i < APP_QUEUE_DEPTH; ++i) {
        app_message_t message;
        if (!rt_status_take(&message)) break;
        event(&message, now);
    }
    if (activity_until) {
        if (now >= activity_until) {
            activity_until = 0;
            on = 0;
            outputs();
        } else if (now >= deadline) {
            on = !on;
            deadline = now + blink_ms;
            outputs();
        }
    } else if (remaining && now >= deadline) {
        if (on) {
            on = 0;
            --remaining;
            deadline = now + 100u;
        } else {
            on = 1;
            deadline = now + on_ms;
        }
        outputs();
    }
}
void status_run(void)
{
    status_init();
    for (;;) {
        status_step();
        rt_sleep(10);
    }
}
