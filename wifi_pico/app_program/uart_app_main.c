/* Kernel entry and ownership only. Protocol work lives in task-context modules. */
#include <tk/tkernel.h>
#include <tm/tmonitor.h>
#include "app.h"
#if CNF_SMP != 1 || TK_MAX_CORE != 2 || TM_CONSOLE_UART || !TM_CONSOLE_USB_CDC || USE_SDEV_DRV || TM_WIFI_CYW43
#error "Use the isolated SMP USB-only protocol profile; no sample UART driver/radio."
#endif
IMPORT UW tm_usb_state(void);
extern void fault_reset(void);
static UW comm_stack[8192 / sizeof(UW)] __attribute__((aligned(8)));
static UW sensor_stack[8192 / sizeof(UW)] __attribute__((aligned(8)));
static UW ui_stack[4096 / sizeof(UW)] __attribute__((aligned(8)));
static UW status_stack[4096 / sizeof(UW)] __attribute__((aligned(8)));
static void comm_task(INT code, void *info) { (void)code; (void)info; communication_run(); }
static void sensor_task(INT code, void *info) { (void)code; (void)info; sensor_emulator_run(); }
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
    rt_log("[START] Wi-Fi Pico UART SMP single-board test\n");
    tm_printf((UB *)"[BUILD] course=%s SMP=%u cores=%u UART0=PROTOCOL UART_FAULT=%u WIFI=none\n",
              INF2004_COURSE_COMMIT, CNF_SMP, TK_MAX_CORE, UART_FAULT);
    rt_log("[TEST] Sensor endpoint is a kernel task on this board, not a bare-metal core\n");
    int error = rt_setup();
    if (error) { rt_fatal("IPC setup", error); }
    fault_reset(); /* Before either owner task can touch its test-only state. */
    start((FP)status_task, TP_PRC2, 9, status_stack, sizeof(status_stack));
    start((FP)ui_task, TP_PRC2, 8, ui_stack, sizeof(ui_stack));
    start((FP)sensor_task, TP_PRC1, 6, sensor_stack, sizeof(sensor_stack));
    start((FP)comm_task, TP_PRC1, 5, comm_stack, sizeof(comm_stack));
    for (;;) {
        if (tm_usb_state() == 3u) {
            rt_log("[READY] GP20 CONFIG; GP21 retrieve; GP22 clear. Maker Pi GP6 status LED\n");
            rt_log("[READY] Loopback jumpers: GP0 -> GP5 and GP4 -> GP1; AHT10 not used\n");
        }
        rt_sleep(5000); /* Existing initial task; no empty production-task placeholders. */
    }
}
