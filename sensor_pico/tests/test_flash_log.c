/* Unit tests for flash_log.c, running against a fake flash that enforces the real flash rules. */
#include "test.h"
#include "flash_log.h"
#include "fake_flash.h"

#define REGION_OFFSET (PICO_FLASH_SIZE_BYTES - FLOG_REGION_BYTES)

static void image_with(uint8_t *img, unsigned n_bytes, uint8_t value) {
    memset(img, 0xFF, FLOG_PAGE_BYTES);
    memset(img, value, n_bytes);
}

static void start(void) { fake_flash_reset(); CHECK_EQ(flog_init(), FLOG_OK); }

static void test_init_on_empty_region(void) {
    start();
    CHECK_EQ(flog_current_page(), 0);
    CHECK(!flog_is_full());
    CHECK(flog_page_is_empty(0));
}

static void test_program_and_read_back(void) {
    start();
    uint8_t img[FLOG_PAGE_BYTES], out[FLOG_PAGE_BYTES];
    image_with(img, 10, 0xA5);
    CHECK_EQ(flog_program_page(img), FLOG_OK);
    flog_read_page(0, out);
    CHECK_BYTES(out, img, FLOG_PAGE_BYTES);
    CHECK(!flog_page_is_empty(0));
}

static void test_same_image_is_unchanged(void) {
    start();
    uint8_t img[FLOG_PAGE_BYTES];
    image_with(img, 10, 0xA5);
    CHECK_EQ(flog_program_page(img), FLOG_OK);
    CHECK_EQ(flog_program_page(img), FLOG_UNCHANGED);
    CHECK_EQ(fake_flash_programs, 1);                 /* no second program for an identical image */
}

static void test_page_can_be_filled_in_steps(void) {
    start();                                          /* what repeated flushes do to one page */
    uint8_t a[FLOG_PAGE_BYTES], b[FLOG_PAGE_BYTES], out[FLOG_PAGE_BYTES];
    image_with(a, 10, 0x12);
    memcpy(b, a, sizeof b); memset(b + 10, 0x34, 6);  /* a plus more data: only 1 -> 0 changes */
    CHECK_EQ(flog_program_page(a), FLOG_OK);
    CHECK_EQ(flog_program_page(b), FLOG_OK);
    flog_read_page(0, out);
    CHECK_BYTES(out, b, FLOG_PAGE_BYTES);
}

static void test_zero_to_one_is_refused(void) {
    start();
    uint8_t a[FLOG_PAGE_BYTES], b[FLOG_PAGE_BYTES], out[FLOG_PAGE_BYTES];
    image_with(a, 4, 0x0F);
    image_with(b, 4, 0xF0);                           /* would need bits to go 0 -> 1 */
    CHECK_EQ(flog_program_page(a), FLOG_OK);
    CHECK_EQ(flog_program_page(b), FLOG_ERR_ILLEGAL);
    flog_read_page(0, out);
    CHECK_BYTES(out, a, FLOG_PAGE_BYTES);             /* flash untouched by the refused write */
}

static void test_advance_moves_to_next_page(void) {
    start();
    uint8_t img[FLOG_PAGE_BYTES];
    image_with(img, 10, 0x11);
    flog_program_page(img);
    CHECK(flog_advance());
    CHECK_EQ(flog_current_page(), 1);
    CHECK_EQ(flog_program_page(img), FLOG_OK);        /* goes to page 1, not page 0 */
    CHECK_EQ(fake_flash_programs, 2);
}

static void test_reboot_starts_on_a_fresh_page(void) {
    start();
    uint8_t img[FLOG_PAGE_BYTES];
    image_with(img, 10, 0x11);
    flog_program_page(img);                           /* page 0 is partly filled; power is lost here */
    CHECK_EQ(flog_init(), FLOG_OK);                   /* "reboot": the module forgets its position */
    CHECK_EQ(flog_current_page(), 1);                 /* the used page is not reopened */
    flog_program_page(img);
    flog_advance();
    flog_program_page(img);
    CHECK_EQ(flog_init(), FLOG_OK);
    CHECK_EQ(flog_current_page(), 3);                 /* pages 0, 1 and 2 are in use */
}

static void test_erase_all(void) {
    start();
    uint8_t img[FLOG_PAGE_BYTES];
    image_with(img, 10, 0x11);
    flog_program_page(img);
    flog_advance();
    flog_program_page(img);
    CHECK(flog_erase_all());
    CHECK_EQ(flog_current_page(), 0);
    CHECK(!flog_is_full());
    for (unsigned p = 0; p < FLOG_PAGE_COUNT; p++) CHECK(flog_page_is_empty(p));
    CHECK_EQ(fake_flash_erases, FLOG_REGION_BYTES / FLASH_SECTOR_SIZE);
}

static void test_full_region_stops(void) {
    start();
    uint8_t img[FLOG_PAGE_BYTES];
    image_with(img, 10, 0x11);
    for (unsigned p = 0; p < FLOG_PAGE_COUNT; p++) {
        CHECK_EQ(flog_program_page(img), FLOG_OK);
        bool more = flog_advance();
        CHECK_EQ(more, p + 1 < FLOG_PAGE_COUNT);      /* the last advance reports "no room" */
    }
    CHECK(flog_is_full());
    CHECK_EQ(flog_program_page(img), FLOG_ERR_FULL);  /* a full log refuses writes, it does not wrap */
    CHECK_EQ(flog_init(), FLOG_ERR_FULL);             /* and is still full after a reboot */
}

static void test_program_failure_is_reported(void) {
    start();
    uint8_t img[FLOG_PAGE_BYTES];
    image_with(img, 10, 0x11);
    fake_flash_fail_next_program();
    CHECK_EQ(flog_program_page(img), FLOG_ERR_PROGRAM);
    CHECK(flog_page_is_empty(0));
    CHECK_EQ(flog_program_page(img), FLOG_OK);        /* and the next attempt works */
}

static void test_verify_failure_is_reported(void) {
    start();
    uint8_t img[FLOG_PAGE_BYTES];
    image_with(img, 10, 0x11);                        /* last byte stays 0xFF; the fake clears it */
    fake_flash_corrupt_next_program();
    CHECK_EQ(flog_program_page(img), FLOG_ERR_VERIFY);
}

static void test_only_the_log_region_is_touched(void) {
    start();
    uint8_t img[FLOG_PAGE_BYTES];
    image_with(img, 10, 0x11);
    for (unsigned p = 0; p < FLOG_PAGE_COUNT; p++) { flog_program_page(img); flog_advance(); }
    flog_erase_all();
    flog_program_page(img);
    for (uint32_t i = 0; i < REGION_OFFSET; i++) {    /* the program image area below the region */
        if ((uint8_t)fake_flash_mem[i] != 0xFF) { CHECK_EQ((uint8_t)fake_flash_mem[i], 0xFF); break; }
    }
}

int main(void) {
    RUN(test_init_on_empty_region);
    RUN(test_program_and_read_back);
    RUN(test_same_image_is_unchanged);
    RUN(test_page_can_be_filled_in_steps);
    RUN(test_zero_to_one_is_refused);
    RUN(test_advance_moves_to_next_page);
    RUN(test_reboot_starts_on_a_fresh_page);
    RUN(test_erase_all);
    RUN(test_full_region_stops);
    RUN(test_program_failure_is_reported);
    RUN(test_verify_failure_is_reported);
    RUN(test_only_the_log_region_is_touched);
    TESTS_DONE();
}
