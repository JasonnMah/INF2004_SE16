/*
 * Course-kernel boundary. Do not include standard size_t/Pico SDK headers here:
 * this port's tk/syslib.h has its own size_t typedef. The protocol is compiled
 * separately with tiny UART/time adapters and its original standard-C headers.
 */
#include <tk/tkernel.h>
#include <tm/tmonitor.h>
#include <sys/sysdef.h>
#include "app.h"
#include "rx_ring.h"
#if APP_COMBINED
#include "upload.h"
static ID upload_queue, upload_results;
static UW upload_storage[16], result_storage[8];
#endif

static ID rx_flags[2], command_queue, status_queue;
static rx_ring_t rings[2];
#define QUEUE_WORDS (APP_QUEUE_DEPTH * (sizeof(app_message_t) + sizeof(INT)) / sizeof(UW))
static UW command_storage[QUEUE_WORDS] __attribute__((aligned(8)));
static UW status_storage[QUEUE_WORDS] __attribute__((aligned(8)));

#define UART0_ADDRESS 0x40034000u
#define UART1_ADDRESS 0x40038000u
#define UART_IRQ0 20u
#define UART_IRQ1 21u
#define U_DR 0x00u
#define U_ECR 0x04u
#define U_FR 0x18u
#define U_IBRD 0x24u
#define U_FBRD 0x28u
#define U_LCR 0x2cu
#define U_CR 0x30u
#define U_IFLS 0x34u
#define U_IMSC 0x38u
#define U_MIS 0x40u
#define U_ICR 0x44u
/* hardware status masks */
#define U_RX_EMPTY 0x10u
#define U_TX_FULL 0x20u
#define U_ERROR_BITS 0x780u
#define U_RX_INTERRUPTS 0x7d0u /* RX, receive-timeout, FE/PE/BE/OE. Never TX IRQ. */
/* kernel notification objects */
#define RX_NOTIFY 1u            /* Received UART data needs attention */
#define COMMAND_NOTIFY 2u       /* A UI command */

static UW uart_address(unsigned unit) { return unit ? UART1_ADDRESS : UART0_ADDRESS; }
unsigned long long rt_now_ms(void)
{
    SYSTIM time;
    ER error = tk_get_otm(&time); /* Monotonic kernel uptime, not RTC/UTC. */
    if (error < E_OK) { rt_fatal("tk_get_otm", error); }
    return ((UD)(UW)time.hi << 32) | time.lo;
}
void rt_sleep(unsigned milliseconds) { (void)tk_dly_tsk(milliseconds); }
void rt_log(const char *text) { tm_printf((UB *)"%s", text); }
void rt_numbers(const char *format, unsigned a, unsigned b, unsigned c)
{ tm_printf((UB *)format, a, b, c); }
void rt_fatal(const char *operation, int error)
{
    for (;;) {
        tm_printf((UB *)"[FAIL] %s error=%d\n", operation, error);
        rt_sleep(1000);
    }
}
int rt_setup(void)
{
    const T_CFLG flag = {.flgatr = TA_TFIFO, .iflgptn = 0};
    for (unsigned i = 0; i < 2; ++i) {
        rx_ring_init(&rings[i]);
        rx_flags[i] = tk_cre_flg(&flag);
        if (rx_flags[i] <= E_OK) { return rx_flags[i]; }
    }
    T_CMBF queue = {
        .mbfatr = TA_TFIFO | TA_USERBUF,
        .bufsz = sizeof(command_storage),
        .maxmsz = sizeof(app_message_t),
        .bufptr = command_storage,
    };
    command_queue = tk_cre_mbf(&queue);
    if (command_queue <= E_OK) { return command_queue; }
    queue.bufptr = status_storage;
    queue.bufsz = sizeof(status_storage);
    status_queue = tk_cre_mbf(&queue);
    if (status_queue <= E_OK) return status_queue;
#if APP_COMBINED
    queue.bufptr=upload_storage;queue.bufsz=sizeof(upload_storage);queue.maxmsz=sizeof(upload_job_t);
    upload_queue=tk_cre_mbf(&queue);
    if(upload_queue<=E_OK) return upload_queue;
    queue.bufptr=result_storage;queue.bufsz=sizeof(result_storage);queue.maxmsz=sizeof(int);
    upload_results=tk_cre_mbf(&queue);
    if(upload_results<=E_OK) return upload_results;
#endif
    return E_OK;
}
static int receive_message(ID queue, app_message_t *message)
{
    INT size = tk_rcv_mbf(queue, message, TMO_POL);
    if (size == (INT)sizeof(*message)) { return 1; }
    if (size != E_TMOUT) { rt_fatal("tk_rcv_mbf", size); }
    return 0;
}
int rt_command_send(const app_message_t *message)
{
    ER error = tk_snd_mbf(command_queue, message, sizeof(*message), TMO_POL);
    if (error == E_TMOUT) { return 0; }
    if (error < E_OK) { rt_fatal("command IPC", error); }
    error = tk_set_flg(rx_flags[0], COMMAND_NOTIFY);
    if (error < E_OK) { rt_fatal("command notification", error); }
    return 1;
}
int rt_command_take(app_message_t *message) { return receive_message(command_queue, message); }
int rt_status_send(const app_message_t *message)
{
    ER error = tk_snd_mbf(status_queue, message, sizeof(*message), TMO_POL);
    if (error == E_TMOUT) { return 0; }
    if (error < E_OK) { rt_fatal("status IPC", error); }
    return 1;
}
int rt_status_take(app_message_t *message) { return receive_message(status_queue, message); }
void rt_wait(unsigned unit, unsigned timeout_ms)
{
    UINT observed;
    ER error = tk_wai_flg(rx_flags[unit], RX_NOTIFY | COMMAND_NOTIFY,
                         TWF_ORW | TWF_BITCLR, &observed, timeout_ms);
    if (error != E_OK && error != E_TMOUT) { rt_fatal("UART notification wait", error); }
}
/* The course PRIMASK helpers have no compiler memory clobber. Pair them
 * with compiler barriers so ring accesses cannot move outside this local
 * critical section. This still protects only the same-core ISR handoff. */
static UINT rx_lock(void)
{
    UINT saved;
    DI(saved);
    __asm__ volatile ("" ::: "memory");
    return saved;
}
static void rx_unlock(UINT saved)
{
    __asm__ volatile ("" ::: "memory");
    EI(saved);
}
static void require_uart_owner(unsigned unit)
{
    if (unit > 1 || tk_get_prc() != 1) { rt_fatal("UART owner must be core 0", -1); }
}
unsigned rt_rx_take(unsigned unit, unsigned char *bytes, unsigned capacity, unsigned *fault)
{
    require_uart_owner(unit);
    /* Both this consumer and its ISR are permanently on core 0. This masks
     * ONLY their same-core handoff; cross-core UI/status state uses kernel IPC.
     * At most 64 bytes are copied under the mask. Parsing happens afterwards. */
    UINT saved = rx_lock();
    unsigned count = rx_ring_take(&rings[unit], bytes, capacity, fault);
    rx_unlock(saved);
    return count;
}
static void uart_rx_interrupt(UINT intno)
{
    unsigned unit = intno == UART_IRQ0 ? 0u : 1u;
    UW base = uart_address(unit);
    rx_ring_t *ring = &rings[unit];
    ++ring->stats.irq_count;
    UW mis = in_w(base + U_MIS);
    out_w(base + U_ICR, U_RX_INTERRUPTS);
    if (mis & U_ERROR_BITS) { rx_ring_fault(ring, RX_FAULT_HARDWARE); }
    /* No parsing, logging, blocking calls or task-context protocol work here. */
    for (unsigned count = 0; count < 64 && !(in_w(base + U_FR) & U_RX_EMPTY); ++count) {
        UW value = in_w(base + U_DR);
        if (value & 0xf00u) {
            ++ring->stats.dropped;
            rx_ring_fault(ring, RX_FAULT_HARDWARE);
        } else {
            rx_ring_push(ring, (unsigned char)value);
        }
    }
    if (mis & U_ERROR_BITS) { out_w(base + U_ECR, 0); }
    /* Legal non-waiting API inside the course TA_HLNG wrapper. Coalescing is
     * intentional: tasks drain the ring, not one notification per byte. */
    if (tk_set_flg(rx_flags[unit], RX_NOTIFY) < E_OK) {
        ++ring->stats.notify_errors;
    }
}
int rt_uart_open(unsigned unit)
{
    require_uart_owner(unit);
    UW base = uart_address(unit);
    UW reset = unit ? RESETS_RESET_UART1 : RESETS_RESET_UART0;
    UINT irq = unit ? UART_IRQ1 : UART_IRQ0;
    DisableInt(irq);
    /* Atomic SET/CLR aliases avoid read/modify/write of another reset bit. */
    out_w(RESETS_RESET + 0x2000u, reset);
    out_w(RESETS_RESET + 0x3000u, reset);
    unsigned spins = 100000;
    while (!(in_w(RESETS_RESET_DONE) & reset)) {
        if (--spins == 0) { return E_TMOUT; }
    }
    out_w(base + U_CR, 0);
    out_w(base + U_IMSC, 0);
    out_w(base + U_ICR, 0x7ffu);
    unsigned divisor = (4u * CLK_PERI_FREQ + 115200u / 2u) / 115200u;
    out_w(base + U_IBRD, divisor >> 6);
    out_w(base + U_FBRD, divisor & 63u);
    out_w(base + U_LCR, 0x70u); /* 8 data bits, no parity, 1 stop, FIFO enabled. */
    out_w(base + U_IFLS, 0); /* RX at 1/8 FIFO plus receive-timeout IRQ. */
    unsigned tx = unit ? 4u : 0u, rx = tx + 1u;
    out_w(GPIO(tx), GPIO_IE | GPIO_DRIVE_4MA | GPIO_SHEMITT);
    out_w(GPIO(rx), GPIO_IE | GPIO_DRIVE_4MA | GPIO_PUE | GPIO_SHEMITT);
    out_w(GPIO_CTRL(tx), GPIO_CTRL_FUNCSEL_UART);
    out_w(GPIO_CTRL(rx), GPIO_CTRL_FUNCSEL_UART);
    const T_DINT definition = {.intatr = TA_HLNG, .inthdr = (FP)uart_rx_interrupt};
    ER error = tk_def_int(irq, &definition);
    if (error < E_OK) { return error; }
    ClearInt(irq);
    EnableInt(irq, 2); /* Same priority convention as the course UART driver. */
    out_w(base + U_CR, 0x301u); /* UARTEN + TXE + RXE; no flow control. */
    out_w(base + U_IMSC, U_RX_INTERRUPTS);
    return E_OK;
}
void rt_uart_write(unsigned unit, const unsigned char *bytes, unsigned length)
{
    require_uart_owner(unit);
    UW base = uart_address(unit);
    unsigned long long started = rt_now_ms();
    for (unsigned i = 0; i < length; ++i) {
        while (in_w(base + U_FR) & U_TX_FULL) {
            /* Interrupts remain enabled; yield when the FIFO is full. A stuck
             * TX cannot trap the task forever or block the USB service. */
            if (rt_now_ms() - started >= 250u) {
                UINT saved = rx_lock();
                rx_ring_fault(&rings[unit], RX_FAULT_TX);
                rx_unlock(saved);
                return;
            }
            rt_sleep(1);
        }
        out_w(base + U_DR, bytes[i]);
    }
}
void rt_trace(const char *name, unsigned reports, unsigned unit)
{
    /* Only each UART's core-0 owner accesses these history slots; UI (unit 2)
     * neither reads nor writes them. Quiet output must still report new errors. */
    static app_rx_stats_t previous[2];
    app_rx_stats_t stats = {0};
    unsigned show = APP_VERBOSE_DIAGNOSTICS || reports == 1;
    if (unit < 2) {
        require_uart_owner(unit);
        UINT saved = rx_lock();
        stats = rings[unit].stats;
        rx_unlock(saved);
        show |= stats.dropped != previous[unit].dropped ||
                stats.faults != previous[unit].faults ||
                stats.notify_errors != previous[unit].notify_errors;
        previous[unit] = stats;
    }
    if (!show) { return; }
    tm_printf((UB *)"[HEALTH] %s tk_prc=%d rp2040_core=%u reports=%u irq=%u bytes=%u dropped=%u faults=%u notify_errors=%u\n",
              name, tk_get_prc(), (UINT)in_w(SIO_CPUID_ADDR), reports,
              stats.irq_count, stats.bytes_seen, stats.dropped, stats.faults, stats.notify_errors);
}
void rt_buttons_init(void)
{
    for (unsigned pin = 20; pin <= 22; ++pin) {
        out_w(GPIO_OE_CLR, 1u << pin);
        out_w(GPIO(pin), GPIO_IE | GPIO_DRIVE_4MA | GPIO_PUE | GPIO_SHEMITT);
        out_w(GPIO_CTRL(pin), GPIO_CTRL_FUNCSEL_SIO);
    }
}
unsigned rt_button_low(unsigned pin) { return !(in_w(GPIO_IN) & (1u << pin)); }
void rt_led_init(void)
{
    out_w(GPIO_OUT_CLR, 1u << 6);
    out_w(GPIO(6), GPIO_DRIVE_4MA | GPIO_SHEMITT);
    out_w(GPIO_CTRL(6), GPIO_CTRL_FUNCSEL_SIO);
    out_w(GPIO_OE_SET, 1u << 6);
}
void rt_led_set(unsigned on) { out_w(on ? GPIO_OUT_SET : GPIO_OUT_CLR, 1u << 6); }

#if APP_COMBINED
int rt_upload_send(const upload_job_t *job)
{
    ER e=tk_snd_mbf(upload_queue,job,sizeof(*job),TMO_POL);
    if(e==E_TMOUT) return 0;
    if(e<E_OK) rt_fatal("upload queue",e);
    return 1;
}
int rt_upload_take(upload_job_t *job)
{
    INT n=tk_rcv_mbf(upload_queue,job,TMO_POL);
    if(n==E_TMOUT) return 0;
    if(n!=(INT)sizeof(*job)) rt_fatal("upload receive",n);
    return 1;
}
void rt_upload_result_send(int result)
{
    /* One outstanding upload: a full result queue is an ownership bug. */
    ER e=tk_snd_mbf(upload_results,&result,sizeof(result),TMO_POL);
    if(e<E_OK) rt_fatal("upload result",e);
}
int rt_upload_result_take(void)
{
    int result;
    INT n=tk_rcv_mbf(upload_results,&result,TMO_POL);
    if(n==E_TMOUT) return 0;
    if(n!=(INT)sizeof(result)) rt_fatal("upload result receive",n);
    return result;
}
#endif
