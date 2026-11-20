#include "fake_flash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char     fake_flash_mem[PICO_FLASH_SIZE_BYTES];
unsigned fake_flash_programs, fake_flash_erases;
static int fail_next, corrupt_next;

static void contract(int ok, const char *what) {
    if (!ok) { fprintf(stderr, "FAKE FLASH: contract violated: %s\n", what); abort(); }
}

void fake_flash_reset(void) {
    memset(fake_flash_mem, 0xFF, sizeof fake_flash_mem);
    fake_flash_programs = fake_flash_erases = 0;
    fail_next = corrupt_next = 0;
}
void fake_flash_fail_next_program(void)    { fail_next = 1; }
void fake_flash_corrupt_next_program(void) { corrupt_next = 1; }

void flash_range_program(uint32_t off, const uint8_t *d, size_t n) {
    contract(off % FLASH_PAGE_SIZE == 0, "program offset not page aligned");
    contract(n % FLASH_PAGE_SIZE == 0, "program size not a whole number of pages");
    contract(off + n <= PICO_FLASH_SIZE_BYTES, "program past the end of flash");
    for (size_t i = 0; i < n; i++) fake_flash_mem[off + i] &= d[i];      /* 1 -> 0 only */
    if (corrupt_next) { fake_flash_mem[off + n - 1] = 0x00; corrupt_next = 0; }
    fake_flash_programs++;
}

void flash_range_erase(uint32_t off, size_t n) {
    contract(off % FLASH_SECTOR_SIZE == 0, "erase offset not sector aligned");
    contract(n % FLASH_SECTOR_SIZE == 0, "erase size not a whole number of sectors");
    contract(off + n <= PICO_FLASH_SIZE_BYTES, "erase past the end of flash");
    memset(fake_flash_mem + off, 0xFF, n);
    fake_flash_erases++;
}

int flash_safe_execute(void (*func)(void *), void *param, uint32_t timeout_ms) {
    (void)timeout_ms;
    if (fail_next) { fail_next = 0; return 1; }
    func(param);
    return 0;
}
