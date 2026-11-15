/* A RAM stand-in for the RP2040 flash, with the real rules: erase sets bytes to 0xFF in whole 4 KiB
 * sectors, programming works on whole 256-byte pages and can only turn 1 bits into 0 bits. */
#ifndef FAKE_FLASH_H
#define FAKE_FLASH_H
#include <stdint.h>
#include "hardware/flash.h"

extern unsigned fake_flash_programs, fake_flash_erases;     /* how many operations the code performed */

void fake_flash_reset(void);                                /* erase everything, clear counters and faults */
void fake_flash_fail_next_program(void);                    /* the next flash_safe_execute reports failure */
void fake_flash_corrupt_next_program(void);                 /* the next program silently stores a wrong last byte */
#endif
