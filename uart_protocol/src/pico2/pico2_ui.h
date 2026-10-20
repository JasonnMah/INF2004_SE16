#ifndef PICO2_UI_H
#define PICO2_UI_H

#include "pico2_protocol.h"

#define PICO2_UI_BUTTON_PIN 20u
#define PICO2_UI_RETRIEVE_PIN 21u
#define PICO2_UI_CLEAR_PIN 22u
#define PICO2_UI_LED_PIN 6u

typedef struct
{
    bool raw_pressed;
    bool stable_pressed;
    uint64_t changed_at_ms;
} pico2_ui_button_t;

typedef struct
{
    pico2_ctx_t *protocol;
    pico2_ui_button_t config_button;
    pico2_ui_button_t retrieve_button;
    pico2_ui_button_t clear_button;
    bool config_pending;
    bool retrieval_pending;
    bool clear_pending;
    bool configured;
    bool led_on;
    uint8_t flashes_remaining;
    uint32_t flash_on_ms;
    uint64_t led_deadline_ms;
} pico2_ui_t;

// The caller initializes the protocol context first, including config values.
void pico2_ui_init(pico2_ui_t *ui, pico2_ctx_t *protocol);
// Call every loop, alongside UART dispatch and protocol timeout processing.
// Presses while a command or its LED indication is in progress are not queued.
void pico2_ui_tick(pico2_ui_t *ui);

#endif
