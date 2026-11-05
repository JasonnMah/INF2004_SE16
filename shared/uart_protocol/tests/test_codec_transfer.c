/* Current Sensor encoder format boundary/golden tests and two-page UART fixture. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "encoder.h"

int main(int argc, char **argv)
{
    assert(argc == 2);
    /* Rejected ranges must not partially write a record. */
    enc_t limits, original;
    enc_reset_page(&limits);
    original = limits;
    const uint16_t invalid[] = {0, 4096, UINT16_MAX};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        assert(enc_put_start(&limits, 3000, 1700000000u, invalid[i], true) == ENC_BAD_ARG);
        assert(!memcmp(&limits, &original, sizeof(limits)));
    }
    assert(enc_put_start(&limits, 3000, 1700000000u, 4095, true) == ENC_OK);
    assert(limits.nib == 19); /* BOOT 2 + TIMEBASE 13 + anchor 4. */
    assert(limits.page[6] == 0xff && (limits.page[7] >> 4) == 0xf);
    enc_reset_page(&limits);
    assert(enc_put_start(&limits, 3000, 1700000000u, 1, false) == ENC_OK);
    assert(limits.nib == 17);
    /* Leave one nibble too little for a start record and CHECK. */
    limits.nib = ENC_PAGE_NIBBLES - ENC_CHECK_NIBBLES - 16;
    original = limits;
    assert(enc_put_start(&limits, 3000, 1700000000u, 1, false) == ENC_NO_ROOM);
    assert(!memcmp(&limits, &original, sizeof(limits)));
    uint8_t pages[2 * ENC_PAGE_BYTES];
    enc_t encoder;
    enc_reset_page(&encoder);
    assert(enc_put_start(&encoder, 3000, 1700000000u, 60, true) == ENC_OK);
    assert(enc_put_sample(&encoder, 3025) == ENC_OK);
    assert(enc_put_sample(&encoder, 3050) == ENC_OK);
    /* Consecutive +1 values confirm deltas use the previous reading. */
    const uint8_t expected[] = {0x73, 0x71, 0x65, 0x53, 0xf1, 0x00,
                                0x03, 0xc8, 0x11, 0x81, 0x1f};
    assert(!memcmp(encoder.page, expected, sizeof(expected)));
    assert(enc_seal(&encoder));
    memcpy(pages, encoder.page, ENC_PAGE_BYTES);
    enc_reset_page(&encoder);
    assert(enc_put_sample(&encoder, 3100) == ENC_OK);
    assert(enc_put_error(&encoder) == ENC_OK);
    assert(enc_seal(&encoder));
    memcpy(pages + ENC_PAGE_BYTES, encoder.page, ENC_PAGE_BYTES);
    FILE *file = fopen(argv[1], "wb");
    assert(file);
    assert(fwrite(pages, 1, sizeof(pages), file) == sizeof(pages));
    assert(fclose(file) == 0);
    puts("PASS: interval12 boundaries, nibble alignment, no-room atomicity and two-page UART fixture");
    return 0;
}
