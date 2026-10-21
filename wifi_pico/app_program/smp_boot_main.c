/*
 * INF2004 milestone 1: course RP2040 SMP boot and USB diagnostics only.
 * No UART, Sensor emulator, buttons, networking or application protocol.
 */
#include <tk/tkernel.h>
#include <tm/tmonitor.h>
#include <sys/sysdef.h>

#if CNF_SMP != 1 || !TK_SUPPORT_SMP || TK_MAX_CORE != 2
#error "This application requires the course two-core SMP build."
#endif
#if !TM_CONSOLE_USB_CDC || TM_CONSOLE_UART || USE_SDEV_DRV || TM_WIFI_CYW43
#error "Use USB diagnostics, no UART console, no sample drivers and no radio."
#endif

/* Existing course USB API. Only its service task calls TinyUSB. */
IMPORT UW tm_usb_state(void);

#define DIAG_STACK_BYTES 4096
#define DIAG_PRIORITY    8
#define BOTH_CORES       0x3U

/* Tasks own their stacks/counters. A kernel event flag handles shared state;
 * local interrupt masking is never used for cross-core synchronization.
 * Application stacks are static; the unchanged kernel/USB service uses its
 * existing bounded RAM allocator during startup. */
static UW diag_stacks[2][DIAG_STACK_BYTES / sizeof(UW)]
    __attribute__((aligned(8)));
static ID observed_cores;

static void wait_for_usb(void)
{
    /* Yield to USB while waiting for the serial monitor's DTR. */
    while (tm_usb_state() != 3U) {
        (void)tk_dly_tsk(100);
    }
    (void)tk_dly_tsk(300);
}

static void fail(const char *operation, ER error)
{
    /* Repeat failures so a monitor opened late can still diagnose them. */
    for (;;) {
        wait_for_usb();
        tm_printf((UB *)"[FAIL] %s error=%d\n", operation, error);
        (void)tk_dly_tsk(1000);
    }
}

static void diagnostic_task(INT stacd, void *exinf)
{
    UW heartbeat = 0;
    const UINT expected_core = (UINT)stacd;
    (void)exinf;

    for (;;) {
        wait_for_usb();
        const INT processor = tk_get_prc();
        const UINT physical_core = in_w(SIO_CPUID_ADDR);
        if (physical_core != expected_core ||
            processor != (INT)expected_core + 1) {
            fail("task affinity/core identity", E_SYS);
        }
        tm_printf((UB *)"[CORE] task=diag%d tk_prc=%d rp2040_core=%d heartbeat=%u\n",
                  stacd, processor, (INT)physical_core, ++heartbeat);
        const ER error = tk_set_flg(observed_cores, 1U << physical_core);
        if (error < E_OK) {
            fail("tk_set_flg", error);
        }
        (void)tk_dly_tsk(700); /* About 1 s including the USB settle delay. */
    }
}

static void print_profile(void)
{
    tm_printf((UB *)"[BOOT] INF2004 Wi-Fi Pico SMP boot milestone 1\n");
    tm_printf((UB *)"[BUILD] course=%s\n", INF2004_COURSE_COMMIT);
    tm_printf((UB *)"[BUILD] SMP=%d cores=%d console=USB_CDC UART0=FREE WIFI=none\n",
              CNF_SMP, TK_MAX_CORE);
}

EXPORT INT usermain(void)
{
    const T_CFLG flag = {.flgatr = TA_TFIFO, .iflgptn = 0};
    observed_cores = tk_cre_flg(&flag);
    if (observed_cores <= E_OK) {
        fail("tk_cre_flg", observed_cores);
    }
    wait_for_usb();
    print_profile();

    for (INT core = 0; core < 2; ++core) {
        const T_CTSK task = {
            .tskatr = TA_HLNG | TA_RNG3 | TA_USERBUF | TA_ASSPRC,
            .task = (FP)diagnostic_task,
            .itskpri = DIAG_PRIORITY,
            .stksz = DIAG_STACK_BYTES,
            .bufptr = diag_stacks[core],
            .assprc = core == 0 ? TP_PRC1 : TP_PRC2,
        };
        const ID id = tk_cre_tsk(&task);
        if (id <= E_OK) {
            fail("tk_cre_tsk", id);
        }
        const ER error = tk_sta_tsk(id, core);
        if (error < E_OK) {
            fail("tk_sta_tsk", error);
        }
    }

    /* Returning shuts the kernel down. The existing initial task instead
     * coordinates these boot diagnostics. This is not the production design. */
    for (;;) {
        UINT observed = 0;
        const ER error = tk_wai_flg(observed_cores, BOTH_CORES,
                                   TWF_ANDW | TWF_BITCLR, &observed, 3000);
        wait_for_usb();
        print_profile(); /* Also supplies context when the monitor reopens. */
        if (error == E_OK && (observed & BOTH_CORES) == BOTH_CORES) {
            tm_printf((UB *)"[CHECK] Both cores reported through kernel IPC (mask=0x3)\n");
        } else {
            tm_printf((UB *)"[FAIL] Both-core report missing: error=%d mask=0x%x\n",
                      error, observed);
        }
        (void)tk_dly_tsk(3000);
    }
}
