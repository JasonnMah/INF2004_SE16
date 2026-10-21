#include "app.h"
#include "ui_logic.h"
unsigned app_button_event(app_button_t *b, unsigned low, unsigned long long now)
{
    if (low != b->raw) { b->raw = low; b->changed = now; }
    if (low == b->stable || now - b->changed < 30u) { return 0; }
    b->stable = low; /* Release also needs 30 ms before the next press can count. */
    return low;
}
unsigned app_flash_count(unsigned status)
{
    return status == APP_STATUS_FAILED ? 3u : status == APP_STATUS_RETRIEVE ? 2u : 1u;
}
unsigned app_flash_on_ms(unsigned status)
{
    switch (status) {
    case APP_STATUS_CONFIG: return 200;
    case APP_STATUS_RETRIEVE: return 150;
    case APP_STATUS_CLEAR: return 500;
    default: return 100;
    }
}
unsigned app_flash_duration(unsigned status)
{
    unsigned flashes = app_flash_count(status);
    return flashes * app_flash_on_ms(status) + (flashes - 1u) * 100u;
}
