/* RAM storage used by the live Sensor UART test. This file describes the
 * stored state and the functions main.c calls; it does not access hardware. */
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
    /* Encoder page currently being filled by new readings. */
    enc_t current;
    /* Finished pages, kept in RAM rather than written to flash. */
    uint8_t pages[LIVE_LOG_CAPACITY];
    /* Separate fixed copy for UART, so new readings cannot change retry bytes. */
    uint8_t snapshot[LIVE_LOG_CAPACITY];
    /* Byte counts, not sample counts. A page may contain many readings. */
    size_t pages_length, snapshot_length;
    /* CONFIG time in Unix seconds; sampling interval also arrives in seconds. */
    uint32_t timestamp, interval_seconds;
    /* Successful stored readings and failed sensor reads are counted separately. */
    uint32_t samples, read_errors;
    /* Sampling enabled; new time record needed; BOOT record written;
     * no page space left; fixed transfer copy currently in use. */
    bool running, need_timebase, boot_written, full, snapshot_held;
} live_logger_t;

/* Start empty and paused. Also used by CLEAR, so this discards RAM contents. */
void live_logger_init(live_logger_t *logger);
/* Accept only 1..4095 whole minutes expressed in seconds; keep existing readings. */
bool live_logger_configure(live_logger_t *logger, uint32_t timestamp, uint32_t seconds);
/* temperature_centi is degrees C times 100 (2452 means 24.52 C).
 * sensor_ok=false records a missing reading when a timebase already exists. */
bool live_logger_append(live_logger_t *logger, bool sensor_ok, int16_t temperature_centi);
/* Return a fixed copy and its byte length. Keep it unchanged until release().
 * The returned bytes remain owned by this logger; the caller must not edit them. */
bool live_logger_snapshot(live_logger_t *logger, const uint8_t **bytes, size_t *length);
/* Allow reuse of the transfer copy, while retaining the working readings. */
void live_logger_release(live_logger_t *logger);
/* Clear RAM and pause sampling; refuse while UART still uses the transfer copy. */
bool live_logger_clear(live_logger_t *logger);
#endif
