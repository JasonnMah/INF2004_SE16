#include "live_logger.h"
#include <string.h>

void live_logger_init(live_logger_t *logger)
{
    memset(logger, 0, sizeof(*logger));
    enc_reset_page(&logger->current);
}

bool live_logger_configure(live_logger_t *logger, uint32_t timestamp, uint32_t seconds)
{
    /* UART uses seconds; JZ's TIMEBASE stores whole minutes. Reject a value we
     * cannot represent instead of silently rounding it or changing the codec. */
    if (!seconds || seconds % 60u || seconds / 60u > UINT16_MAX ||
        logger->full || logger->snapshot_held) return false;
    logger->timestamp = timestamp;
    logger->interval_seconds = seconds;
    logger->need_timebase = true;
    logger->running = true;
    return true;
}

static bool advance_page(live_logger_t *logger)
{
    /* Reserve room for the next current page before moving this one. On full,
     * retain every existing byte for retrieval and stop rather than overwrite. */
    if (logger->pages_length + 2u * ENC_PAGE_BYTES > LIVE_LOG_CAPACITY) {
        logger->full = true;
        logger->running = false;
        return false;
    }
    if (!enc_seal(&logger->current)) return false;
    memcpy(logger->pages + logger->pages_length, logger->current.page, ENC_PAGE_BYTES);
    logger->pages_length += ENC_PAGE_BYTES;
    enc_reset_page(&logger->current);
    return true;
}

static enc_result_t encode_reading(live_logger_t *logger, bool ok, int16_t temperature)
{
    if (!ok) return enc_put_error(&logger->current);
    if (logger->need_timebase)
        return enc_put_start(&logger->current, temperature, logger->timestamp,
                             (uint16_t)(logger->interval_seconds / 60u), !logger->boot_written);
    return enc_put_sample(&logger->current, temperature);
}

bool live_logger_append(live_logger_t *logger, bool sensor_ok, int16_t temperature_centi)
{
    if (!logger->running) return false;
    if (!sensor_ok) {
        ++logger->read_errors;
        /* Like JZ's main: no sensor-error record before a first TIMEBASE/anchor. */
        if (logger->need_timebase) return false;
    }
    enc_result_t result = encode_reading(logger, sensor_ok, temperature_centi);
    if (result == ENC_NO_ROOM && advance_page(logger))
        result = encode_reading(logger, sensor_ok, temperature_centi);
    if (result != ENC_OK) return false;
    if (sensor_ok) {
        ++logger->samples;
        logger->need_timebase = false;
        logger->boot_written = true;
    }
    return true;
}

bool live_logger_snapshot(live_logger_t *logger, const uint8_t **bytes, size_t *length)
{
    if (logger->snapshot_held) return false;
    size_t size = logger->pages_length;
    if (logger->current.nib) size += ENC_PAGE_BYTES;
    if (size > sizeof(logger->snapshot)) return false;
    memcpy(logger->snapshot, logger->pages, logger->pages_length);
    if (logger->current.nib) {
        /* Seal a COPY. Sampling can continue; the UART source stays identical
         * until final ACK or retry exhaustion calls release(). No flash writes. */
        enc_t sealed = logger->current;
        if (!enc_seal(&sealed)) return false;
        memcpy(logger->snapshot + logger->pages_length, sealed.page, ENC_PAGE_BYTES);
    }
    logger->snapshot_length = size;
    logger->snapshot_held = true;
    *bytes = logger->snapshot;
    *length = size;
    return true;
}

void live_logger_release(live_logger_t *logger)
{
    logger->snapshot_held = false;
}

bool live_logger_clear(live_logger_t *logger)
{
    if (logger->snapshot_held) return false;
    live_logger_init(logger); /* RAM only. CLEAR also pauses this test's sampling. */
    return true;
}
