/* Read-only compatibility check using JZ's unchanged encoder. No Sensor firmware is modified. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "encoder.h"

int main(int argc, char **argv)
{
    assert(argc == 2);
    uint8_t pages[2 * ENC_PAGE_BYTES];
    enc_t encoder;
    enc_reset_page(&encoder);
    assert(enc_put_start(&encoder, 3000, 1700000000u, 60, true) == ENC_OK);
    assert(enc_put_sample(&encoder, 3025) == ENC_OK);
    assert(enc_put_sample(&encoder, 3050) == ENC_OK);
    /* Consecutive +1 values confirm deltas use the previous reading. */
    assert(encoder.page[10] == 0x11);
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
    puts("PASS: unchanged Sensor encoder produces two complete pages for UART compatibility tests");
    return 0;
}
