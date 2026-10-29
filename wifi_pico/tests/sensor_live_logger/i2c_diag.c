/* Optional diagnostic only. Linker wrappers keep JZ's source and return values
 * unchanged. Capture during init, report later: no USB wait/print on the bus path.
 * Do not wrap i2c_*_timeout_us: those helpers are inline in SDK 2.2.0.
 */
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "aht10.h"
#include "i2c_diag.h"

#define INIT_RECORDS 4u
#define CAPTURE_BYTES 6u

typedef struct {
    bool write;
    uint8_t address, bytes[CAPTURE_BYTES];
    size_t requested, captured;
    int result;
    uint32_t elapsed_us;
} init_record_t;

static init_record_t records[INIT_RECORDS];
static unsigned record_count;
static bool capturing, initial_sda, initial_scl;
static aht10_status_t init_result;

extern aht10_status_t __real_aht10_init(i2c_inst_t *i2c);
extern int __real_i2c_write_blocking_until(i2c_inst_t *i2c, uint8_t address,
    const uint8_t *bytes, size_t length, bool nostop, absolute_time_t until);
extern int __real_i2c_read_blocking_until(i2c_inst_t *i2c, uint8_t address,
    uint8_t *bytes, size_t length, bool nostop, absolute_time_t until);

static init_record_t *begin_record(bool write, uint8_t address, size_t length)
{
    if (!capturing || record_count == INIT_RECORDS) return NULL;
    init_record_t *record = &records[record_count++];
    *record = (init_record_t){.write = write, .address = address, .requested = length};
    return record;
}

static void capture_bytes(init_record_t *record, const uint8_t *bytes, size_t length)
{
    record->captured = length < CAPTURE_BYTES ? length : CAPTURE_BYTES;
    memcpy(record->bytes, bytes, record->captured);
}

aht10_status_t __wrap_aht10_init(i2c_inst_t *i2c)
{
    record_count = 0;
    initial_sda = gpio_get(0);
    initial_scl = gpio_get(1);
    capturing = true;
    init_result = __real_aht10_init(i2c);
    capturing = false;
    return init_result;
}

int __wrap_i2c_write_blocking_until(i2c_inst_t *i2c, uint8_t address,
    const uint8_t *bytes, size_t length, bool nostop, absolute_time_t until)
{
    init_record_t *record = begin_record(true, address, length);
    if (record) capture_bytes(record, bytes, length);
    uint64_t started = time_us_64();
    int result = __real_i2c_write_blocking_until(i2c, address, bytes, length, nostop, until);
    if (record) {
        record->result = result;
        record->elapsed_us = (uint32_t)(time_us_64() - started);
    }
    return result;
}

int __wrap_i2c_read_blocking_until(i2c_inst_t *i2c, uint8_t address,
    uint8_t *bytes, size_t length, bool nostop, absolute_time_t until)
{
    init_record_t *record = begin_record(false, address, length);
    uint64_t started = time_us_64();
    int result = __real_i2c_read_blocking_until(i2c, address, bytes, length, nostop, until);
    if (record) {
        record->result = result;
        record->elapsed_us = (uint32_t)(time_us_64() - started);
        if (result > 0) capture_bytes(record, bytes, (size_t)result < length ? (size_t)result : length);
    }
    return result;
}

void sensor_i2c_trace_report(void)
{
    printf("[I2C INIT] Saved startup trace; SDA=%u SCL=%u before reset\n",
           initial_sda ? 1u : 0u, initial_scl ? 1u : 0u);
    for (unsigned i = 0; i < record_count; ++i) {
        const init_record_t *record = &records[i];
        char hex[2u * CAPTURE_BYTES + 1u];
        for (size_t n = 0; n < record->captured; ++n)
            snprintf(hex + 2u * n, sizeof(hex) - 2u * n, "%02x", record->bytes[n]);
        if (!record->captured) { hex[0] = '-'; hex[1] = '\0'; }
        const char *outcome = "unexpected count";
        if (record->result == (int)record->requested) outcome = "complete";
        else if (record->result == PICO_ERROR_TIMEOUT) outcome = "timeout";
        else if (record->result < 0) outcome = "bus/address error";
        else if (record->write) outcome = "short write";
        else outcome = "short read";
        printf("[I2C INIT] #%u %s addr=0x%02x bytes=%s expected=%u returned=%d elapsed_us=%" PRIu32 " %s\n",
               i + 1u, record->write ? "WRITE" : "READ", record->address, hex,
               (unsigned)record->requested, record->result, record->elapsed_us, outcome);
    }
    printf("[I2C INIT] result=%s calls=%u; no extra transactions; measurements not traced\n",
           aht10_status_str(init_result), record_count);
}
