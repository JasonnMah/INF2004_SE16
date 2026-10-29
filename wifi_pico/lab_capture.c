/* Lab diagnostics only: opaque bytes, never temperature decoding. */
#include "app.h"
#include "lab_capture.h"
#include "protocol/crc16.h"
#include <string.h>
static const unsigned char *source;
static unsigned total, offset, capture_id, active;
static const char hex[] = "0123456789abcdef";
void lab_capture_cancel(void) { active = 0; }
unsigned lab_capture_active(void) { return active; }
void lab_capture_begin(const unsigned char *bytes, unsigned length, unsigned completion)
{
    source = bytes; total = length; offset = 0; capture_id = completion; active = 1;
    rt_numbers("[BINARY BEGIN] completion=%u length=%u crc16_x25=%04x\n",
               completion, length, crc16_x25(bytes, length));
}
void lab_capture_step(void)
{
    if (!active) { return; }
    if (offset == total) {
        rt_numbers("[BINARY END] completion=%u\n", capture_id, 0, 0);
        active = 0;
        return;
    }
    /* A single USB log call keeps each line intact across core diagnostics.
     * Limit output to 32 data bytes per iteration; UART processing continues.
     * Commands remain gated so the captured source cannot be overwritten. */
    char line[96];
    const char prefix[] = "[BINARY] offset=";
    unsigned n = sizeof(prefix) - 1;
    memcpy(line, prefix, n);
    for (unsigned i = 0; i < 4; ++i) { line[n++] = hex[(offset >> (12 - 4*i)) & 15u]; }
    memcpy(line + n, " hex=", 5); n += 5;
    unsigned count = total - offset;
    if (count > 32) { count = 32; }
    for (unsigned i = 0; i < count; ++i) {
        unsigned byte = source[offset + i];
        line[n++] = hex[byte >> 4]; line[n++] = hex[byte & 15u];
    }
    line[n++] = '\n'; line[n] = 0;
    rt_log(line);
    offset += count;
}
