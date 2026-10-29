#ifndef SENSOR_LIVE_LOGGER_H
#define SENSOR_LIVE_LOGGER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "encoder.h"
#include "protocol/storage_profile.h"

/* A bounded RAM experiment, not JZ's persistent flash logger. Full encoder pages
 * keep their original CHECK records and 0xFF padding. UART payload size is separate. */
#define LIVE_LOG_CAPACITY STORAGE_TRANSFER_CAPACITY
_Static_assert(LIVE_LOG_CAPACITY % ENC_PAGE_BYTES == 0, "RAM capacity must hold whole encoder pages");

typedef struct {
    enc_t current;
    uint8_t pages[LIVE_LOG_CAPACITY];
    uint8_t snapshot[LIVE_LOG_CAPACITY];
    size_t pages_length, snapshot_length;
    uint32_t timestamp, interval_seconds;
    uint32_t samples, read_errors;
    bool running, need_timebase, boot_written, full, snapshot_held;
} live_logger_t;

void live_logger_init(live_logger_t *logger);
bool live_logger_configure(live_logger_t *logger, uint32_t timestamp, uint32_t seconds);
bool live_logger_append(live_logger_t *logger, bool sensor_ok, int16_t temperature_centi);
bool live_logger_snapshot(live_logger_t *logger, const uint8_t **bytes, size_t *length);
void live_logger_release(live_logger_t *logger);
bool live_logger_clear(live_logger_t *logger);
#endif
