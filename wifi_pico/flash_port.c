/* RP2040 only. No kernel calls, flash reads, IRQ handlers or second-core flash
 * execution are allowed between ROM exit-XIP and the copied boot2 return.
 *
 * Core 1 cooperatively enters a RAM loop from its normal task, outside kernel
 * calls/locks. Core 0 keeps interrupts enabled while waiting for that task;
 * masking them earlier could prevent the kernel tick waking it. Only after
 * the RAM acknowledgement does core 0 mask its own interrupts and write.
 * The SIO FIFO belongs to the kernel and is deliberately left untouched.
 * This implementation needs physical qualification before application use.
 */
#include "flash_port.h"
#include <tk/tkernel.h>
#include <stdint.h>

#if CNF_SMP != 1 || TK_MAX_CORE != 2
#error "Flash rendezvous requires the course RP2040 SMP profile"
#endif
#define RAM_CODE __attribute__((section(".ram_code"), noinline))
#define REG32(address) (*(volatile uint32_t *)(uintptr_t)(address))
#define CORE_ID REG32(0xd0000000u)
#define TIMER_LOW REG32(0x40054028u)
#define SRAM_BEGIN 0x20000000u
#define SRAM_END 0x20042000u

static volatile uint32_t request, parked, parks;
static unsigned operations;
static uint32_t boot2_copy[64] __attribute__((aligned(4)));
typedef struct {
    void (*connect)(void);
    void (*exit_xip)(void);
    void (*erase)(uint32_t, uint32_t, uint32_t, uint8_t);
    void (*program)(uint32_t, const uint8_t *, uint32_t);
    void (*flush)(void);
} rom_functions_t;
static rom_functions_t rom;

static inline __attribute__((always_inline)) void barrier(void) { __asm__ volatile("dmb" ::: "memory"); }
static inline __attribute__((always_inline)) uint32_t mask_interrupts(void)
{
    uint32_t saved;
    __asm__ volatile("mrs %0, primask\n cpsid i" : "=r"(saved) :: "memory");
    return saved;
}
static inline __attribute__((always_inline)) void restore_interrupts(uint32_t saved)
{ __asm__ volatile("msr primask, %0" :: "r"(saved) : "memory"); }

void RAM_CODE flash_port_core1_poll(void)
{
    if (CORE_ID != 1 || !request) return;
    uint32_t saved = mask_interrupts();
    if (request) {
        ++parks;
        barrier();
        parked = 1;
        barrier();
        while (request) __asm__ volatile("nop" ::: "memory");
        barrier();
        parked = 0;
        barrier();
    }
    restore_interrupts(saved);
}

static void RAM_CODE flash_operation(uint32_t offset, const uint8_t *page)
{
    uint32_t pads[6];
    for (unsigned i = 0; i < 6; ++i) pads[i] = REG32(0x40020004u + 4u * i);
    rom.connect();
    rom.exit_xip();
    if (page) rom.program(offset, page, 256);
    else rom.erase(offset, 4096, 4096, 0x20);
    rom.flush();
    ((void (*)(void))((uintptr_t)boot2_copy | 1u))();
    for (unsigned i = 0; i < 6; ++i) REG32(0x40020004u + 4u * i) = pads[i];
}

static bool dma_idle(void)
{
    /* RP2040: twelve DMA channels; CTRL_TRIG.BUSY is bit 24. */
    for (unsigned channel = 0; channel < 12; ++channel)
        if (REG32(0x5000000cu + 0x40u * channel) & (1u << 24)) return false;
    return true;
}
static bool operate(uint32_t offset, const uint8_t *page)
{
    if (CORE_ID != 0 || offset >= BATCH_STORE_BYTES ||
        offset % (page ? 256u : 4096u)) return false;
    if (page && ((uintptr_t)page < SRAM_BEGIN || (uintptr_t)page > SRAM_END - 256))
        return false;
    uint32_t started = TIMER_LOW;
    for (;;) {
        while (parked || !dma_idle()) {
            if ((uint32_t)(TIMER_LOW - started) >= 1000000u) return false;
            (void)tk_dly_tsk(1);
        }
        request = 1;
        barrier();
        while (!parked) {
            if ((uint32_t)(TIMER_LOW - started) >= 1000000u) {
                request = 0;
                barrier();
                return false; /* No flash operation started. */
            }
            (void)tk_dly_tsk(1);
        }
        uint32_t saved = mask_interrupts();
        barrier();
        /* A radio DMA may have started while we were waiting for core 1.
         * Recheck with both CPUs stopped before disabling XIP. Never stall a DMA
         * engine mid-read from flash; let the owner finish it, then try again. */
        if (!dma_idle()) {
            request = 0; barrier(); restore_interrupts(saved);
            if ((uint32_t)(TIMER_LOW - started) >= 1000000u) return false;
            (void)tk_dly_tsk(1); continue;
        }
        flash_operation(WIFI_CACHE_FLASH_OFFSET + offset, page);
        barrier();
        request = 0; /* XIP is restored before the other core can leave RAM. */
        barrier();
        restore_interrupts(saved);
        ++operations;
        return true;
    }
}
static bool erase_sector(void *context, uint32_t offset)
{ (void)context; return operate(offset, NULL); }
static bool program_page(void *context, uint32_t offset, const uint8_t page[256])
{
    (void)context;
    if (offset >= BATCH_STORE_BYTES) return false;
    const uint8_t *before = (const uint8_t *)(uintptr_t)(0x10000000u + WIFI_CACHE_FLASH_OFFSET + offset);
    for (unsigned i = 0; i < 256; ++i) if (before[i] != 0xff) return false;
    return operate(offset, page);
}
static uintptr_t lookup(unsigned char first, unsigned char second)
{
    typedef void *(*lookup_t)(uint16_t *, uint32_t);
    /* ROM metadata contains 16-bit pointers at these documented RP2040
     * addresses. Explicit loads avoid GCC treating low ROM as a null object. */
    uint32_t find_address, table_address;
    __asm__ volatile("ldrh %0, [%1]" : "=r"(find_address) : "r"(0x18u) : "memory");
    __asm__ volatile("ldrh %0, [%1]" : "=r"(table_address) : "r"(0x14u) : "memory");
    lookup_t find = (lookup_t)(uintptr_t)find_address;
    uint16_t *table = (uint16_t *)(uintptr_t)table_address;
    return (uintptr_t)find(table, first | ((uint32_t)second << 8));
}
bool flash_port_init(batch_flash_t *flash)
{
    if (CORE_ID != 0 || !flash ||
        (uintptr_t)flash_operation < SRAM_BEGIN || (uintptr_t)flash_operation >= SRAM_END ||
        (uintptr_t)flash_port_core1_poll < SRAM_BEGIN || (uintptr_t)flash_port_core1_poll >= SRAM_END)
        return false;
    rom.connect = (void (*)(void))lookup('I', 'F');
    rom.exit_xip = (void (*)(void))lookup('E', 'X');
    rom.erase = (void (*)(uint32_t, uint32_t, uint32_t, uint8_t))lookup('R', 'E');
    rom.program = (void (*)(uint32_t, const uint8_t *, uint32_t))lookup('R', 'P');
    rom.flush = (void (*)(void))lookup('F', 'C');
    if (!rom.connect || !rom.exit_xip || !rom.erase || !rom.program || !rom.flush) return false;
    for (unsigned i = 0; i < 64; ++i) boot2_copy[i] = REG32(0x10000000u + 4u * i);
    *flash = (batch_flash_t){.mapped = (const uint8_t *)(uintptr_t)(0x10000000u + WIFI_CACHE_FLASH_OFFSET),
                            .erase = erase_sector, .program = program_page};
    return true;
}
unsigned flash_port_park_count(void) { barrier(); return parks; }
unsigned flash_port_operation_count(void) { return operations; }
