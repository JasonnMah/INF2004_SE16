#ifndef WIFI_UI_LOGIC_H
#define WIFI_UI_LOGIC_H
typedef struct { unsigned raw, stable; unsigned long long changed; } app_button_t;
unsigned app_button_event(app_button_t *button, unsigned low, unsigned long long now);
unsigned app_flash_count(unsigned status);
unsigned app_flash_on_ms(unsigned status);
unsigned app_flash_duration(unsigned status);
#endif
