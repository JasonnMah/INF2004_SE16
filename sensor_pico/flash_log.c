#include "flash_log.h"

#include <string.h>
#include <stdio.h>

#include "pico/version.h"
#include "hardware/flash.h"

/* SDK 2.x has flash_safe_execute(). SDK 1.x does not, so on 1.x we disable interrupts around the
 * flash operation instead (fine for this single-core, bare-metal prototype: core 1 is never started). */
#if PICO_SDK_VERSION_MAJOR >= 2
#include "pico/flash.h"
#else
#include "hardware/sync.h"
#endif

extern char __flash_binary_end;                 /* linker symbol: end of the program image */

#define FLOG_REGION_OFFSET  (PICO_FLASH_SIZE_BYTES - FLOG_REGION_BYTES)
#define FLASH_TIMEOUT_MS    1000u

_Static_assert(FLOG_PAGE_BYTES == FLASH_PAGE_SIZE, "log page must equal the flash page size");
_Static_assert(FLOG_REGION_BYTES % FLASH_SECTOR_SIZE == 0, "region must be whole sectors");
_Static_assert(FLOG_REGION_BYTES < PICO_FLASH_SIZE_BYTES, "region larger than flash");

static uint32_t cur_page;
static bool     full;

static const uint8_t *page_ptr(uint32_t page) {
    return (const uint8_t *)(uintptr_t)(XIP_BASE + FLOG_REGION_OFFSET + page * FLOG_PAGE_BYTES);
}

typedef struct { uint32_t offset; const uint8_t *data; } prog_args_t;

static void do_program(void *p) {
    const prog_args_t *a = (const prog_args_t *)p;
    flash_range_program(a->offset, a->data, FLOG_PAGE_BYTES);
}

static void do_erase(void *p) {
    flash_range_erase(*(const uint32_t *)p, FLASH_SECTOR_SIZE);
}

/* Run fn(arg) with flash access made safe. Returns true on success. */
static bool run_flash_op(void (*fn)(void *), void *arg) {
#if PICO_SDK_VERSION_MAJOR >= 2
    return flash_safe_execute(fn, arg, FLASH_TIMEOUT_MS) == PICO_OK;
#else
    uint32_t ints = save_and_disable_interrupts();
    fn(arg);
    restore_interrupts(ints);
    return true;
#endif
}

bool flog_page_is_empty(uint32_t page) {
    const uint8_t *f = page_ptr(page);
    for (uint32_t i = 0; i < FLOG_PAGE_BYTES; i++)
        if (f[i] != 0xFF) return false;
    return true;
}

void flog_read_page(uint32_t page, uint8_t out[FLOG_PAGE_BYTES]) {
    memcpy(out, page_ptr(page), FLOG_PAGE_BYTES);
}

flog_status_t flog_init(void) {
    uintptr_t image_end = (uintptr_t)&__flash_binary_end;
    if (image_end > (uintptr_t)XIP_BASE &&
        image_end - (uintptr_t)XIP_BASE > FLOG_REGION_OFFSET)
        return FLOG_ERR_OVERLAP;

    full = false;
    for (cur_page = 0; cur_page < FLOG_PAGE_COUNT; cur_page++) {
        if (flog_page_is_empty(cur_page)) return FLOG_OK;
    }
    cur_page = FLOG_PAGE_COUNT - 1;
    full = true;
    return FLOG_ERR_FULL;
}

bool flog_erase_all(void) {
    bool ok = true;
    for (int32_t s = (int32_t)(FLOG_REGION_BYTES / FLASH_SECTOR_SIZE) - 1; s >= 0; s--) {
        uint32_t off = FLOG_REGION_OFFSET + (uint32_t)s * FLASH_SECTOR_SIZE;
        if (!run_flash_op(do_erase, &off)) {
            printf("flash: erase of sector %d failed\n", (int)s);
            ok = false;
        }
    }
    cur_page = 0;
    full = false;
    return ok;
}

flog_status_t flog_program_page(const uint8_t image[FLOG_PAGE_BYTES]) {
    if (full) return FLOG_ERR_FULL;

    const uint8_t *f = page_ptr(cur_page);
    bool same = true;
    for (uint32_t i = 0; i < FLOG_PAGE_BYTES; i++) {
        if ((f[i] & image[i]) != image[i]) return FLOG_ERR_ILLEGAL;   /* would need 0 -> 1 */
        if (f[i] != image[i]) same = false;
    }
    if (same) return FLOG_UNCHANGED;

    prog_args_t a = { FLOG_REGION_OFFSET + cur_page * FLOG_PAGE_BYTES, image };
    if (!run_flash_op(do_program, &a))
        return FLOG_ERR_PROGRAM;
    if (memcmp(f, image, FLOG_PAGE_BYTES) != 0) return FLOG_ERR_VERIFY;
    return FLOG_OK;
}

bool flog_advance(void) {
    if (cur_page + 1 >= FLOG_PAGE_COUNT) {
        full = true;
        return false;
    }
    cur_page++;
    return true;
}

uint32_t flog_current_page(void) { return cur_page; }
bool     flog_is_full(void)      { return full; }