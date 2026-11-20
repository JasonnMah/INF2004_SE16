#include "temp_log_print.h"
#include <stdio.h>
#include <inttypes.h>

typedef struct { uint32_t timestamp, interval; } table_time_t;
static void temperature(int16_t t)
{
    int32_t magnitude = t < 0 ? -(int32_t)t : t;
    printf("%s%ld.%02ld C", t < 0 ? "-" : "", (long)(magnitude / 100),
           (long)(magnitude % 100));
}
static void encoding_kind(const temp_decoded_sample_t *sample)
{
    if (sample->is_anchor) { printf("ANCHOR"); }
    else { printf("DELTA %+d", sample->steps); }
}
static void row(const temp_decoded_sample_t *sample, void *context)
{
    table_time_t *time = context;
    uint64_t timestamp = (uint64_t)time->timestamp + (uint64_t)sample->index * time->interval;
    printf("%u        %" PRIu64 "       ", (unsigned)sample->index + 1, timestamp);
    temperature(sample->temperature_x100);
    printf("       "); encoding_kind(sample); puts("");
}
void temp_log_print(const uint8_t *blob, size_t length, uint32_t timestamp, uint32_t interval)
{
    uint16_t count;
    temp_decode_status_t status = temp_decode(blob, length, NULL, NULL, &count);
    if (status != TEMP_DECODE_OK)
    {
        printf("[DECODE] Invalid log: %s\n", temp_decode_status_string(status));
        return;
    }
    if (count == 0)
    {
        puts("[DECODE] Logger is empty; no temperature samples to display.");
        return;
    }
    puts("============================================================");
    puts("                 DECODED TEMPERATURE LOG");
    puts("Sample   Timestamp       Temperature       Encoding");
    puts("------   ----------      -----------       --------");
    table_time_t time = {timestamp, interval};
    (void)temp_decode(blob, length, row, &time, &count);
    printf("Samples decoded : %u\nCompressed bytes: %u (plus 2-byte header)\n",
           count, (unsigned)(length - 2));
    puts("============================================================");
}
void temp_encode_print(int16_t original, const temp_decoded_sample_t *encoding)
{
    printf("[ENCODE] #%u ", (unsigned)encoding->index + 1);
    temperature(original); printf("  "); encoding_kind(encoding);
    // Expose quantization instead of claiming lossless temperature reconstruction.
    printf("  encoded="); temperature(encoding->temperature_x100); puts("");
}
