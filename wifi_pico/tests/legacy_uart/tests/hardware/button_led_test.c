/*
 * Standalone Maker Pi Pico button/indicator check, running on a Pico W.
 * This is a separate firmware image: it does not call any UART protocol code.
 *
 * Board reference: https://www.cytron.io/p-maker-pi-pico
 * The carrier has GPIO indicator LEDs and active-LOW buttons on GP20..GP22.
 * GP6 is unused by this project's UART wiring (GP0/1/4/5) and avoids the
 * carrier's SD (GP10..15), ESP-01 (GP16/17), audio (GP18/19), and RGB (GP28).
 * Leave GP6's header/Grove connection unoccupied while running this check.
 * We use the carrier's GP6 indicator, NOT the Pico W's onboard LED. GP25 on
 * Pico W belongs to the wireless interface and is not a normal LED output.
 */
#include <stdio.h>
#include "pico/stdlib.h"

#define INDICATOR_LED_PIN 6u
#define DEBOUNCE_MS 30u
#define LED_FLASH_MS 100u
#define POLL_MS 1u

/* Each button needs its own history so holding one does not block the others. */
typedef struct
{
    uint pin;
    bool last_reading_pressed;  // Most recent raw sample, before debounce.
    bool stable_pressed;       // Accepted state; stays true until stable release.
    uint64_t changed_at_ms;     // When the raw reading last changed.
} button_t;

/* Return true only when this button makes a debounced released -> pressed edge. */
static bool new_press(button_t *button, uint64_t now_ms)
{
    // Pull-ups make the idle input HIGH. The button connects it to GND (LOW).
    bool pressed = !gpio_get(button->pin);

    if (pressed != button->last_reading_pressed)
    {
        button->last_reading_pressed = pressed;
        button->changed_at_ms = now_ms;
    }

    // Every bounce restarts the timer. Accept neither a press nor a release
    // until the input has stayed at the same level for the full debounce time.
    if (pressed == button->stable_pressed ||
        now_ms - button->changed_at_ms < DEBOUNCE_MS)
    {
        return false;
    }

    button->stable_pressed = pressed;
    // A release re-arms the button but produces no event. A held button cannot
    // print again: its stable state remains pressed until a debounced release.
    return pressed;
}

int main(void)
{
    // CMake enables USB CDC serial for this target only; UART stdio is disabled.
    // Open the USB serial terminal before pressing buttons to see their output.
    // We do not wait for a terminal, so LED testing also works without one.
    stdio_init_all();

    // Zero-initialized states treat each button as released at startup. A
    // button already held during boot will generate one event after debounce.
    button_t buttons[] = {{.pin = 20}, {.pin = 21}, {.pin = 22}};
    for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++)
    {
        gpio_init(buttons[i].pin);
        gpio_set_dir(buttons[i].pin, GPIO_IN);
        gpio_pull_up(buttons[i].pin);
    }

    // The carrier's GPIO indicator is active HIGH. Set the output latch LOW
    // before enabling output so the test starts with the indicator off.
    gpio_init(INDICATOR_LED_PIN);
    gpio_put(INDICATOR_LED_PIN, false);
    gpio_set_dir(INDICATOR_LED_PIN, GPIO_OUT);
    uint64_t led_off_at_ms = 0;

    while (true)
    {
        uint64_t now_ms = time_us_64() / 1000u;
        for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++)
        {
            if (new_press(&buttons[i], now_ms))
            {
                printf("GP%u pressed\n", buttons[i].pin);
                // Start the full pulse after USB output, even if it took time.
                led_off_at_ms = time_us_64() / 1000u + LED_FLASH_MS;
            }
        }

        // No 100 ms blocking sleep: keep scanning all buttons while the LED
        // is on. Nearby/simultaneous presses extend the same shared LED pulse.
        gpio_put(INDICATOR_LED_PIN, time_us_64() / 1000u < led_off_at_ms);
        sleep_ms(POLL_MS);
    }
}
