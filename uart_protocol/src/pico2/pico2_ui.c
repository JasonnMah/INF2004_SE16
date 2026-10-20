#include "pico2_ui.h"
#include "pico/stdlib.h"

#define DEBOUNCE_MS 30u
#define SUCCESS_ON_MS 200u
#define FAILURE_ON_MS 100u
#define FAILURE_OFF_MS 100u

static void start_indication(pico2_ui_t *ui, unsigned flashes, unsigned on_ms, uint64_t now_ms)
{
    ui->flashes_remaining = flashes;
    ui->flash_on_ms = on_ms;
    ui->led_on = true;
    ui->led_deadline_ms = now_ms + ui->flash_on_ms;
    gpio_put(PICO2_UI_LED_PIN, true);
}

static void update_led(pico2_ui_t *ui, uint64_t now_ms)
{
    if (ui->flashes_remaining == 0 || now_ms < ui->led_deadline_ms)
    {
        return;
    }
    if (ui->led_on)
    {
        ui->led_on = false;
        ui->flashes_remaining--;
        ui->led_deadline_ms = now_ms + FAILURE_OFF_MS;
    }
    else
    {
        ui->led_on = true;
        ui->led_deadline_ms = now_ms + ui->flash_on_ms;
    }
    gpio_put(PICO2_UI_LED_PIN, ui->led_on);
}

static bool button_pressed(pico2_ui_button_t *button, unsigned pin, uint64_t now_ms)
{
    bool pressed = !gpio_get(pin);
    if (pressed != button->raw_pressed)
    {
        button->raw_pressed = pressed;
        button->changed_at_ms = now_ms;
    }
    if (pressed == button->stable_pressed || now_ms - button->changed_at_ms < DEBOUNCE_MS)
    {
        return false;
    }
    // Accept both edges only after 30 ms. A held button cannot retrigger, and
    // a bouncing release cannot prematurely re-arm it. Never wait in a loop.
    button->stable_pressed = pressed;
    return pressed;
}

void pico2_ui_init(pico2_ui_t *ui, pico2_ctx_t *protocol)
{
    *ui = (pico2_ui_t){.protocol = protocol};
    gpio_init(PICO2_UI_BUTTON_PIN);
    gpio_set_dir(PICO2_UI_BUTTON_PIN, GPIO_IN);
    gpio_pull_up(PICO2_UI_BUTTON_PIN);
    gpio_init(PICO2_UI_RETRIEVE_PIN);
    gpio_set_dir(PICO2_UI_RETRIEVE_PIN, GPIO_IN);
    gpio_pull_up(PICO2_UI_RETRIEVE_PIN);
    gpio_init(PICO2_UI_CLEAR_PIN);
    gpio_set_dir(PICO2_UI_CLEAR_PIN, GPIO_IN);
    gpio_pull_up(PICO2_UI_CLEAR_PIN);
    // This is the Maker Pi carrier's indicator LED, not Pico W's onboard LED.
    gpio_init(PICO2_UI_LED_PIN);
    gpio_put(PICO2_UI_LED_PIN, false);
    gpio_set_dir(PICO2_UI_LED_PIN, GPIO_OUT);
}

void pico2_ui_tick(pico2_ui_t *ui)
{
    uint64_t now_ms = time_us_64() / 1000u;
    pico2_ctx_t *p2 = ui->protocol;

    // Protocol handlers alone decide whether the matching ACK arrived or the
    // retry budget was exhausted. The UI never interprets frames or ACK bytes.
    if (ui->config_pending && p2->state == P2_FAILED)
    {
        ui->config_pending = false;
        protocol_log(&p2->link, "[UI] CONFIG failed\n");
        start_indication(ui, 3, FAILURE_ON_MS, now_ms);
    }
    else if (ui->config_pending && p2->state == P2_COMPLETE && !p2->pending_command.active)
    {
        ui->config_pending = false;
        ui->configured = true;
        protocol_log(&p2->link, "[UI] CONFIG acknowledged\n");
        start_indication(ui, 1, SUCCESS_ON_MS, now_ms);
    }
    if (ui->retrieval_pending && p2->state == P2_FAILED)
    {
        ui->retrieval_pending = false;
        protocol_log(&p2->link, "[UI] Data retrieval failed\n");
        start_indication(ui, 3, FAILURE_ON_MS, now_ms);
    }
    else if (ui->retrieval_pending && p2->state == P2_COMPLETE && !p2->pending_command.active)
    {
        ui->retrieval_pending = false;
        protocol_log(&p2->link, "[UI] Data retrieval complete\n");
        start_indication(ui, 2, 150u, now_ms);
    }
    if (ui->clear_pending && p2->state == P2_FAILED)
    {
        ui->clear_pending = false;
        protocol_log(&p2->link, "[UI] CLEAR_LOG failed\n");
        start_indication(ui, 3, FAILURE_ON_MS, now_ms);
    }
    else if (ui->clear_pending && p2->state == P2_COMPLETE && !p2->pending_command.active)
    {
        ui->clear_pending = false;
        protocol_log(&p2->link, "[UI] Log cleared\n");
        start_indication(ui, 1, 500u, now_ms);
    }
    update_led(ui, now_ms);

    // Always sample all buttons, including while busy, so ignored presses are
    // consumed and a held button cannot become a delayed command.
    bool config = button_pressed(&ui->config_button, PICO2_UI_BUTTON_PIN, now_ms);
    bool retrieve = button_pressed(&ui->retrieve_button, PICO2_UI_RETRIEVE_PIN, now_ms);
    bool clear = button_pressed(&ui->clear_button, PICO2_UI_CLEAR_PIN, now_ms);
    if (config) { protocol_log(&p2->link, "[UI] GP20 pressed\n"); }
    if (retrieve) { protocol_log(&p2->link, "[UI] GP21 pressed\n"); }
    if (clear) { protocol_log(&p2->link, "[UI] GP22 pressed\n"); }
    if (!config && !retrieve && !clear) { return; }
    if (ui->config_pending || ui->retrieval_pending || ui->clear_pending || ui->flashes_remaining != 0)
    {
        protocol_log(&p2->link, "[UI] Busy; release button and press again when idle\n");
        return;
    }
    // Simultaneous presses prioritize CONFIG, then retrieval, then clear.
    // Lower-priority buttons need a fresh press; clearing is never queued.
    if (config)
    {
        protocol_log(&p2->link, "[UI] Sending CONFIG\n");
        if (pico2_send_config(p2)) { ui->config_pending = true; }
        else
        {
            protocol_log(&p2->link, "[UI] CONFIG failed\n");
            start_indication(ui, 3, FAILURE_ON_MS, now_ms);
        }
    }
    else if (!ui->configured)
    {
        protocol_log(&p2->link, "[UI] Configure first using GP20\n");
    }
    else if (retrieve)
    {
        protocol_log(&p2->link, "[UI] Requesting stored data\n");
        if (pico2_request_data(p2)) { ui->retrieval_pending = true; }
        else
        {
            protocol_log(&p2->link, "[UI] Data retrieval failed\n");
            start_indication(ui, 3, FAILURE_ON_MS, now_ms);
        }
    }
    else
    {
        protocol_log(&p2->link, "[UI] Clearing stored log\n");
        if (pico2_clear_log(p2)) { ui->clear_pending = true; }
        else
        {
            protocol_log(&p2->link, "[UI] CLEAR_LOG failed\n");
            start_indication(ui, 3, FAILURE_ON_MS, now_ms);
        }
    }
}
