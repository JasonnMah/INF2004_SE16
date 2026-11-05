/* Standalone network milestone, NOT the combined UART+HTTP firmware.
 * Reuses the course's priority-4 core-0 radio task for HTTP, not a second
 * preemptible raw-lwIP caller. UI and Status run on core 1 at priorities 8/9.
 * USB remains priority 3. Final combined app will have four roles. */
#include <tk/tkernel.h>
#include <tm/tmonitor.h>
#include "app.h"
#include "buttons.h"
#include "network_retry.h"
#if CNF_SMP != 1 || TK_MAX_CORE != 2 || TM_CONSOLE_UART || !TM_CONSOLE_USB_CDC || !TM_WIFI_CYW43 || !TM_APP_HTTP
#error "Use the isolated SMP USB HTTP build profile."
#endif
IMPORT UW tm_usb_state(void);
extern void http_service_enable(void);
static UW ui_stack[4096/sizeof(UW)] __attribute__((aligned(8)));
static UW status_stack[4096/sizeof(UW)] __attribute__((aligned(8)));
static void ui_task(INT code, void *info)
{
    (void)code; (void)info;
    app_button_t button = {0};
    rt_buttons_init();
    rt_trace("HTTP test UI", 1, 2);
    for (;;) {
        unsigned long long now = rt_now_ms();
        if (app_button_event(&button, rt_button_low(21), now)) {
            rt_log("[UI] GP21 pressed\n");
            app_message_t command = {.kind = APP_RETRIEVE, .at_ms = now};
            if (!rt_command_send(&command)) rt_log("[UI] Command queue full\n");
        }
        rt_sleep(10);
    }
}
static void led_task(INT code, void *info) { (void)code; (void)info; status_run(); }
static void start(FP entry, PRI priority, void *stack, SZ size)
{
    const T_CTSK definition = {.tskatr = TA_HLNG | TA_RNG3 | TA_USERBUF | TA_ASSPRC,
        .task = entry, .itskpri = priority, .stksz = size, .bufptr = stack, .assprc = TP_PRC2};
    ID id = tk_cre_tsk(&definition);
    if (id <= E_OK) rt_fatal("HTTP task create", id);
    ER error = tk_sta_tsk(id, 0);
    if (error < E_OK) rt_fatal("HTTP task start", error);
}
EXPORT INT usermain(void)
{
    while (tm_usb_state() != 3u) rt_sleep(100);
    rt_sleep(300);
    rt_log("[START] Wi-Fi Pico known-binary HTTP test\n");
    rt_log("[BUILD] SMP=1 cores=2 HTTP=KNOWN_BYTES UART0=FREE SensorEmulator=DISABLED\n");
#if HTTP_BUILD_CHECK
    rt_log("[BUILD] BUILD CHECK ONLY: placeholder network settings; POST disabled\n");
#endif
    rt_log("[BUILD] WIFI_DIAGNOSTICS=1; scan and join details enabled\n");
    rt_log("[WIFI] Connection attempt 1/3; retry gap=2000 ms\n");
    rt_numbers("[BUILD] WIFI_NONET_RETRY_MS=%u JOIN_WINDOW_MS=%u\n",
               (unsigned)(WIFI_JOIN_NONET_RETRY_US / 1000),
               (unsigned)(WIFI_JOIN_WINDOW_US / 1000), 0);
    rt_log("[TEST] GP21 uploads; GP20/GP22 unused. No Sensor or UART wiring required\n");
    int error = rt_setup();
    if (error) rt_fatal("HTTP IPC setup", error);
    start((FP)led_task, 9, status_stack, sizeof(status_stack));
    start((FP)ui_task, 8, ui_stack, sizeof(ui_stack));
    http_service_enable();
    for (;;) rt_sleep(1000);
}
