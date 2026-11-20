/*
 * flash_log.h - the reserved 1 MiB log region in on-board flash.
 *
 * The region is reserved from the END of flash, so it does not move when the program grows.
 * Pages are 256 bytes. The log writes one RAM page image at a time to the "current" page. The same
 * page can be programmed repeatedly as it fills, because the encoder only ever turns erased
 * (0xFF) bits into data, and flash programming can only change bits from 1 to 0.
 *
 * Every flash erase or program goes through flash_safe_execute() (Pico SDK 2.x, pico_flash).
 */
#ifndef FLASH_LOG_H
#define FLASH_LOG_H

#include <stdint.h>
#include <stdbool.h>

#ifndef FLOG_REGION_BYTES
#define FLOG_REGION_BYTES (1024u * 1024u)       /* must be a multiple of the 4096-byte sector */
#endif
#define FLOG_PAGE_BYTES   256u
#define FLOG_PAGE_COUNT   (FLOG_REGION_BYTES / FLOG_PAGE_BYTES)

typedef enum {
    FLOG_OK = 0,
    FLOG_UNCHANGED,       /* image already identical to flash: nothing to do */
    FLOG_ERR_FULL,        /* no page left */
    FLOG_ERR_ILLEGAL,     /* image would need a 0 -> 1 change: needs an erase, so refused */
    FLOG_ERR_PROGRAM,     /* flash_safe_execute failed */
    FLOG_ERR_VERIFY,      /* read-back differs from the image */
    FLOG_ERR_OVERLAP      /* the program image reaches into the log region */
} flog_status_t;

/* Find the first page that is entirely 0xFF and make it the current page. Existing pages, including
 * a page torn by a power cut, are left alone. Returns FLOG_OK, FLOG_ERR_FULL or FLOG_ERR_OVERLAP. */
flog_status_t flog_init(void);

/* Erase and read back one sector. Used to service UART between erase steps.
 * Does not reset the write position; call flog_init() after all sectors pass. */
bool flog_erase_sector(uint32_t sector);

/* Erase and verify the whole region (highest sector first), then reset page 0.
 * A failure does not mark partially erased storage as empty or writable. */
bool flog_erase_all(void);

/* Program the 256-byte image to the current page. */
flog_status_t flog_program_page(const uint8_t image[FLOG_PAGE_BYTES]);

/* Move to the next page. Returns false (and marks the log full) if there is none. */
bool flog_advance(void);

uint32_t flog_current_page(void);
bool     flog_is_full(void);

/* Raw access, for dumping. */
void flog_read_page(uint32_t page, uint8_t out[FLOG_PAGE_BYTES]);
bool flog_page_is_empty(uint32_t page);
/* Read-only XIP source; caller freezes the page range before borrowing it. */
const uint8_t *flog_data(void);

#endif
