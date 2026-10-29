/* Kernel entry and ownership only. Protocol work lives in task-context modules. */
#include <tk/tkernel.h>
#include <tm/tmonitor.h>
#include "app.h"
#if CNF_SMP != 1 || TK_MAX_CORE != 2 || TM_CONSOLE_UART || !TM_CONSOLE_USB_CDC || USE_SDEV_DRV || !TM_WIFI_CYW43 || !TM_APP_HTTP || !APP_COMBINED
#error "Use the combined SMP USB UART/HTTP profile."
#endif
IMPORT UW tm_usb_state(void);
extern void http_test_enable(void);
#if APP_FAULT_INJECTION
extern void fault_reset(void);
#endif
static UW comm_stack[8192 / sizeof(UW)] __attribute__((aligned(8)));
#if APP_SENSOR_EMULATOR
static UW sensor_stack[8192 / sizeof(UW)] __attribute__((aligned(8)));
#endif
static UW ui_stack[4096 / sizeof(UW)] __attribute__((aligned(8)));
static UW status_stack[4096 / sizeof(UW)] __attribute__((aligned(8)));
static void comm_task(INT code, void *info) { (void)code; (void)info; communication_run(); }
#if APP_SENSOR_EMULATOR
static void sensor_task(INT code, void *info) { (void)code; (void)info; sensor_emulator_run(); }
#endif
static void ui_task(INT code, void *info) { (void)code; (void)info; ui_run(); }
static void status_task(INT code, void *info) { (void)code; (void)info; status_run(); }

static void start(FP entry, UW affinity, PRI priority, void *stack, SZ size)
{
    const T_CTSK task = {
        .tskatr = TA_HLNG | TA_RNG3 | TA_USERBUF | TA_ASSPRC,
        .task = entry, .itskpri = priority, .stksz = size, .bufptr = stack, .assprc = affinity,
    };
    ID id = tk_cre_tsk(&task);
    if (id <= E_OK) { rt_fatal("tk_cre_tsk", id); }
    ER error = tk_sta_tsk(id, 0);
    if (error < E_OK) { rt_fatal("tk_sta_tsk", error); }
}
EXPORT INT usermain(void)
{
    while (tm_usb_state() != 3u) { rt_sleep(100); }
    rt_sleep(300);
#if APP_SENSOR_EMULATOR
    rt_log("[START] Wi-Fi Pico UART SMP single-board test\n");
#else
    rt_log("[START] Wi-Fi Pico UART + HTTP two-board integration\n");
    rt_log("[BUILD] SensorEmulator=DISABLED application_tasks=4 HTTP=COMPRESSED_BYTES\n");
#endif
    tm_printf((UB *)"[BUILD] course=%s SMP=%u cores=%u UART0=PROTOCOL UART_FAULT=%u DIAGNOSTICS=%u WIFI=cyw43\n",
              INF2004_COURSE_COMMIT, CNF_SMP, TK_MAX_CORE, UART_FAULT, APP_VERBOSE_DIAGNOSTICS);
#if APP_SENSOR_EMULATOR
    rt_log("[TEST] Sensor endpoint is a kernel task on this board, not a bare-metal core\n");
#endif
#if !APP_SENSOR_EMULATOR && UART_FAULT == 2
    rt_log("[TEST] Wi-Fi-only receive-timeout test: first retrieval ignores DATA/END; later requests are normal\n");
#elif !APP_SENSOR_EMULATOR && UART_FAULT == 3
    rt_log("[TEST] Wi-Fi-only final-ACK test: drop the first TRANSFER_END ACK once\n");
#endif
    int error = rt_setup();
    if (error) { rt_fatal("IPC setup", error); }
#if APP_FAULT_INJECTION
    fault_reset(); /* Before either owner task can touch its test-only state. */
#endif
    start((FP)status_task, TP_PRC2, 9, status_stack, sizeof(status_stack));
    start((FP)ui_task, TP_PRC2, 8, ui_stack, sizeof(ui_stack));
#if APP_SENSOR_EMULATOR
    start((FP)sensor_task, TP_PRC1, 6, sensor_stack, sizeof(sensor_stack));
#endif
    start((FP)comm_task, TP_PRC1, 4, comm_stack, sizeof(comm_stack));
    http_test_enable();
    UW ready_printed = 0;
    for (;;) {
        if (tm_usb_state() != 3u) { ready_printed = 0; }
        else if (!ready_printed) {
            ready_printed = 1;
            rt_log("[READY] GP20 CONFIG; GP21 retrieve then upload on next press; GP22 clear. Maker Pi GP6 status LED\n");
#if APP_SENSOR_EMULATOR
            rt_log("[READY] Loopback jumpers: GP0 -> GP5 and GP4 -> GP1; AHT10 not used\n");
#else
            rt_log("[READY] External Sensor: Wi-Fi GP0 TX / GP1 RX; common GND; no same-board loopback\n");
#endif
        }
        rt_sleep(250); /* Reannounce on USB reconnect, without periodic READY spam. */
    }
}
