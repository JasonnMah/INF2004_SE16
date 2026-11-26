#include <tk/tkernel.h>
#include <tm/tmonitor.h>
#include <sys/sysdef.h>
#include <bsp/libbsp.h>
#include "app.h"
#include "rx_ring.h"
#if APP_COMBINED
#include "upload.h"
#include "pc_config.h"
static ID upload_queue, upload_results;
static UW upload_storage[16], result_storage[8];
/* The course permits four message buffers total. UI and status use two;
 * settings and uploads share the existing radio request/result buffers. */
enum { RADIO_UPLOAD = 1, RADIO_CONFIG = 2 };
typedef struct { unsigned kind; upload_job_t upload; unsigned token; } radio_request_t;
typedef struct { unsigned kind; int upload; pc_config_result_t config; } radio_result_t;
static radio_request_t next_request; /* Radio task alone reads/writes this cache. */
static radio_result_t next_result;   /* Communication task alone owns this cache. */
static unsigned request_cached, result_cached;
_Static_assert(sizeof(upload_storage) >= sizeof(radio_request_t) + sizeof(INT), "Radio request storage");
_Static_assert(sizeof(result_storage) >= sizeof(radio_result_t) + sizeof(INT), "Radio result storage");
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
    queue.bufptr=upload_storage;queue.bufsz=sizeof(upload_storage);queue.maxmsz=sizeof(radio_request_t);
    upload_queue=tk_cre_mbf(&queue);
    if(upload_queue<=E_OK) return upload_queue;
    queue.bufptr=result_storage;queue.bufsz=sizeof(result_storage);queue.maxmsz=sizeof(radio_result_t);
    upload_results=tk_cre_mbf(&queue);
    if(upload_results<=E_OK) return upload_results;
    request_cached=result_cached=0;
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

/* Maker Pi Pico GP18 is PWM slice 1, channel A. Status Task alone owns it.
 * This profile has no physical-timer clients; SysTick drives the kernel tick.
 * Keep a future physical-timer profile from silently sharing the same slice. */
#if USE_PTMR
#error "Reserve PWM slice 1 before enabling physical timers with the error buzzer."
#endif
#define BUZZER_PIN 18u
#define BUZZER_HZ 2000u
#define BUZZER_PERIOD (CLK_PLL_SYS_FREQ / BUZZER_HZ)
_Static_assert(BUZZER_PERIOD > 1u && BUZZER_PERIOD <= 65536u, "Buzzer PWM period");
void rt_buzzer_init(void)
{
    /* The course leaves PWM in reset when USE_PTMR=0. Release it without
     * resetting other slices, then configure only slice 1; no PWM IRQ needed. */
    /* Atomic clear avoids racing UART/radio reset release on the other core. */
    out_w(RESETS_RESET + 0x3000u, RESETS_RESET_PWM);
    unsigned wait = 1000u;
    while (!(in_w(RESETS_RESET_DONE) & RESETS_RESET_PWM) && --wait) { }
    if (!wait) rt_fatal("buzzer PWM reset", E_SYS);
    out_w(PWM_CH1_BASE + PWM_CHx_CSR, 0);
    out_w(PWM_CH1_BASE + PWM_CHx_DIV, 1u << 4); /* Divider 1.0, 8:4 fixed point. */
    out_w(PWM_CH1_BASE + PWM_CHx_CTR, 0);
    pwm_set_wrap(BUZZER_PIN, BUZZER_PERIOD - 1u);
    pwm_set_cc(BUZZER_PIN, 0); /* Silent before routing PWM to the piezo. */
    out_w(GPIO(BUZZER_PIN), GPIO_DRIVE_4MA | GPIO_SHEMITT);
    pwm_set_pin(BUZZER_PIN);
    pwm_set_enabled(BUZZER_PIN, 1);
}
void rt_buzzer_set(unsigned on)
{
    /* Hardware generates the 2 kHz tone; the task only times its envelope.
     * Zero duty returns the output LOW, rather than stopping it mid-tone. */
    pwm_set_cc(BUZZER_PIN, on ? BUZZER_PERIOD / 2u : 0u);
}

#if APP_COMBINED
static int radio_request_send(const radio_request_t *request)
{
    ER error=tk_snd_mbf(upload_queue,request,sizeof(*request),TMO_POL);
    if(error==E_TMOUT) return 0;
    if(error<E_OK) rt_fatal("radio request",error);
    return 1;
}
static int radio_request_pending(void)
{
    if(request_cached) return 1;
    INT size=tk_rcv_mbf(upload_queue,&next_request,TMO_POL);
    if(size==E_TMOUT) return 0;
    if(size!=(INT)sizeof(next_request)) rt_fatal("radio request receive",size);
    request_cached=1;
    return 1;
}
int rt_config_request_send(unsigned token)
{
    const radio_request_t request={.kind=RADIO_CONFIG,.token=token};
    return radio_request_send(&request);
}
int rt_config_request_take(unsigned *token)
{
    if(!radio_request_pending() || next_request.kind!=RADIO_CONFIG) return 0;
    *token=next_request.token;request_cached=0;
    return 1;
}
int rt_upload_send(const upload_job_t *job)
{
    const radio_request_t request={.kind=RADIO_UPLOAD,.upload=*job};
    return radio_request_send(&request);
}
int rt_upload_take(upload_job_t *job)
{
    if(!radio_request_pending() || next_request.kind!=RADIO_UPLOAD) return 0;
    *job=next_request.upload;request_cached=0;
    return 1;
}
static void radio_result_send(const radio_result_t *result)
{
    /* One outstanding application operation: a full result queue is a bug. */
    ER error=tk_snd_mbf(upload_results,result,sizeof(*result),TMO_POL);
    if(error<E_OK) rt_fatal("radio result",error);
    error=tk_set_flg(rx_flags[0],COMMAND_NOTIFY);
    if(error<E_OK) rt_fatal("radio result notification",error);
}
static int radio_result_pending(void)
{
    if(result_cached) return 1;
    INT size=tk_rcv_mbf(upload_results,&next_result,TMO_POL);
    if(size==E_TMOUT) return 0;
    if(size!=(INT)sizeof(next_result)) rt_fatal("radio result receive",size);
    result_cached=1;
    return 1;
}
void rt_config_result_send(const pc_config_result_t *result)
{
    const radio_result_t response={.kind=RADIO_CONFIG,.config=*result};
    radio_result_send(&response);
}
int rt_config_result_take(pc_config_result_t *result)
{
    if(!radio_result_pending() || next_result.kind!=RADIO_CONFIG) return 0;
    *result=next_result.config;result_cached=0;
    return 1;
}
void rt_upload_result_send(int result)
{
    const radio_result_t response={.kind=RADIO_UPLOAD,.upload=result};
    radio_result_send(&response);
}
int rt_upload_result_take(void)
{
    if(!radio_result_pending() || next_result.kind!=RADIO_UPLOAD) return 0;
    int result=next_result.upload;result_cached=0;
    return result;
}
#endif
