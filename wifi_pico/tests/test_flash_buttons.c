/* Real debounce helper, sampled at coarse RTOS intervals. No physical GPIO or
 * kernel scheduling is emulated. The linked-firmware check verifies that the
 * flash-test task actually calls this helper with the kernel's elapsed time. */
#include "buttons.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>

int main(void)
{
    for (unsigned interval = 10; interval <= 20; interval += 10) {
        app_button_t button = {0};
        unsigned events = 0;
        for (unsigned time = 0; time <= 60; time += interval)
            events += app_button_event(&button, 1, time);
        assert(events == 1); /* A normal 60 ms press must not require 30 loops. */
        for (unsigned time = 80; time < 2000; time += interval)
            events += app_button_event(&button, 1, time);
        assert(events == 1); /* Holding cannot retrigger. */
        assert(!app_button_event(&button, 0, 2000));
        assert(!app_button_event(&button, 0, 2040));
        assert(!app_button_event(&button, 1, 2060));
        assert(app_button_event(&button, 1, 2100));
    }
    app_button_t button = {0};
    assert(!app_button_event(&button, 1, 11));
    assert(!app_button_event(&button, 0, 19)); /* Press bounce. */
    assert(!app_button_event(&button, 1, 24));
    assert(!app_button_event(&button, 1, 43));
    assert(app_button_event(&button, 1, 61)); /* Jittered sample after 37 ms. */
    assert(!app_button_event(&button, 0, 70));
    assert(!app_button_event(&button, 1, 80)); /* Release bounce cannot rearm. */
    assert(!app_button_event(&button, 1, 120));
    assert(!app_button_event(&button, 0, 140));
    assert(!app_button_event(&button, 0, 180));
    assert(!app_button_event(&button, 1, 190));
    assert(app_button_event(&button, 1, 230));
    button = (app_button_t){0};
    assert(!app_button_event(&button, 1, ULLONG_MAX - 20));
    assert(!app_button_event(&button, 1, 5));
    assert(app_button_event(&button, 1, 10));
    puts("PASS flash buttons: 60 ms taps at 10/20 ms sampling, hold/release, bounce, jitter and uptime wrap");
}
