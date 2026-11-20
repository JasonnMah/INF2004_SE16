/* Qualification firmware only: no Sensor, UART, Wi-Fi, HTTP or automatic ACK.
 * GP21 creates a known pending batch. A later reboot must detect it unchanged.
 * GP20 simulates a PC receipt and releases the test batch. No real upload occurs.
 */
#include <tk/tkernel.h>
#include <tm/tmonitor.h>
#include <sys/sysdef.h>
#include "flash_port.h"
#include "buttons.h"

#if CNF_SMP != 1 || TK_MAX_CORE != 2 || TM_CONSOLE_UART || !TM_CONSOLE_USB_CDC || TM_WIFI_CYW43
#error "Use the isolated SMP USB flash qualification profile"
#endif
IMPORT UW tm_usb_state(void);
static UW stacks[2][4096 / sizeof(UW)] __attribute__((aligned(8)));
static ID commands;
static batch_store_t store;
static uint8_t known[BATCH_BYTES];
static volatile unsigned core1_ticks;

static bool pattern_matches(const uint8_t *data)
{
    for (unsigned i = 0; i < BATCH_BYTES; ++i) if (data[i] != known[i]) return false;
    return true;
}
static bool test_identity(void)
{
    if (store.record.log_sha256[0] != 0x41) return false;
    for (unsigned i = 1; i < 32; ++i) if (store.record.log_sha256[i]) return false;
    return true;
}

static void die(const char *message)
{
    for (;;) {
        tm_printf((UB *)"[FAIL] %s; existing cache retained, no automatic erase\n", message);
        (void)tk_dly_tsk(2000);
    }
}
static void report(void)
{
    tm_printf((UB *)"[CACHE] state=%u generation=%u bytes=%u offset=%u total=%u\n",
        store.record.state, store.record.generation, store.record.length,
        store.record.offset, store.record.total);
    if (store.record.state == BATCH_READY) {
        const uint8_t *data = batch_store_data(&store);
        if (!data || store.record.length != BATCH_BYTES || !pattern_matches(data))
            die("saved test pattern mismatch");
        tm_printf((UB *)"[CACHE] Pending 4096-byte test batch verified; crc16_x25=%04x\n",
                  store.record.data_crc);
    }
    tm_printf((UB *)"[SMP] writer_core=%u core1_ticks=%u park_count=%u flash_operations=%u\n",
        (unsigned)in_w(SIO_CPUID_ADDR), core1_ticks,
        flash_port_park_count(), flash_port_operation_count());
}
static unsigned pressed(unsigned pin)
{ return !(in_w(0xd0000004u) & (1u << pin)); }
static void core1_task(INT code, void *info)
{
    (void)code; (void)info;
    for (unsigned pin = 20; pin <= 21; ++pin) {
        out_w(0xd0000028u, 1u << pin); /* SIO GPIO output-enable clear. */
        out_w(0x4001c004u + pin * 4u, 0x5au); /* Input, Schmitt, pull-up. */
        out_w(0x40014004u + pin * 8u, 5u); /* SIO input selection. */
    }
    app_button_t buttons[2] = {{0}};
    for (;;) {
        flash_port_core1_poll(); /* RAM rendezvous, outside all kernel calls. */
        ++core1_ticks;
        /* A requested 1 ms sleep is rounded by the course kernel's tick.
         * Use elapsed time, as the verified UI does, rather than loop counts. */
        SYSTIM time;
        if (tk_get_otm(&time) < E_OK) die("button clock");
        unsigned long long now = ((unsigned long long)(UW)time.hi << 32) | time.lo;
        for (unsigned i = 0; i < 2; ++i) {
            if (app_button_event(&buttons[i], pressed(20 + i), now)) {
                tm_printf((UB *)"[BUTTON] GP%u accepted; tk_prc=%d rp2040_core=%u\n",
                          20 + i, tk_get_prc(), (unsigned)in_w(SIO_CPUID_ADDR));
                if (tk_set_flg(commands, 1u << i) < E_OK) die("button IPC");
            }
        }
        (void)tk_dly_tsk(1);
    }
}
static void writer_task(INT code, void *info)
{
    (void)code; (void)info;
    batch_flash_t flash;
    if (!flash_port_init(&flash) || !batch_store_open(&store, flash)) die("flash/cache initialization");
    if (store.record.generation && !test_identity()) die("cache belongs to a different application/log");
    for (unsigned i = 0; i < BATCH_BYTES; ++i) known[i] = (uint8_t)i;
    report();
    unsigned usb_before = 0;
    for (;;) {
        unsigned usb = tm_usb_state() == 3u;
        if (usb && !usb_before) {
            tm_printf((UB *)"[START] Wi-Fi Pico SMP flash cache test; UART0 FREE, Wi-Fi OFF\n");
            tm_printf((UB *)"[BUILD] SMP=1 cores=2 reserved=last 16 KiB; Sensor firmware unchanged\n");
            tm_printf((UB *)"[BUILD] buttons=elapsed30ms-v2 kernel_tick_ms=%u\n", CNF_TIMER_PERIOD);
            tm_printf((UB *)"[TEST] GP21 save known 4096 bytes; reboot checks persistence; GP20 SIMULATES receipt\n");
            report();
        }
        usb_before = usb;
        UINT flags = 0;
        ER error = tk_wai_flg(commands, 3u, TWF_ORW | TWF_BITCLR, &flags, 100);
        if (error == E_TMOUT) continue;
        if (error < E_OK) die("command flag");
        if (flags & 2u) {
            tm_printf((UB *)"[FLASH] GP21 command received by writer task\n");
            if (store.record.state == BATCH_READY) {
                tm_printf((UB *)"[CACHE] Pending batch retained; GP21 does not overwrite it\n");
            } else {
                uint8_t identity[32] = {0}; /* Test identity, NOT a real log SHA256. */
                identity[0] = 0x41;
                if (!batch_store_begin(&store, identity, BATCH_BYTES, 0) ||
                    !batch_store_save(&store, known, BATCH_BYTES)) die("save test batch");
                tm_printf((UB *)"[PASS] 4096 test bytes saved and read back from flash\n");
            }
        } else if (flags & 1u) {
            tm_printf((UB *)"[FLASH] GP20 command received by writer task; SIMULATED receipt only\n");
            if (store.record.state == BATCH_READY) {
                if (!batch_store_confirm(&store) || !batch_store_finish(&store)) die("simulated receipt commit");
                tm_printf((UB *)"[PASS] SIMULATED receipt committed; temporary test bytes released; no HTTP occurred\n");
            } else tm_printf((UB *)"[CACHE] No ready test batch to confirm\n");
        }
        report();
    }
}
EXPORT INT usermain(void)
{
    const T_CFLG flag = {.flgatr = TA_TFIFO, .iflgptn = 0};
    commands = tk_cre_flg(&flag);
    if (commands <= E_OK) die("create event flag");
    for (unsigned core = 0; core < 2; ++core) {
        const T_CTSK task = {.tskatr = TA_HLNG | TA_RNG3 | TA_USERBUF | TA_ASSPRC,
            .task = core ? (FP)core1_task : (FP)writer_task, .itskpri = core ? 8 : 7,
            .stksz = sizeof(stacks[core]), .bufptr = stacks[core],
            .assprc = core ? TP_PRC2 : TP_PRC1};
        ID id = tk_cre_tsk(&task);
        if (id <= E_OK || tk_sta_tsk(id, 0) < E_OK) die("create/start flash test task");
    }
    for (;;) (void)tk_dly_tsk(1000);
}
