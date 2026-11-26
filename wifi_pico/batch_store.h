#ifndef WIFI_BATCH_STORE_H
#define WIFI_BATCH_STORE_H
#include <stdbool.h>
#include <stdint.h>

#define BATCH_BYTES 4096u
#define BATCH_MAX_LOG_BYTES (1024u * 1024u)
#define BATCH_STORE_PAGE 256u
#define BATCH_STORE_SECTOR 4096u
#define BATCH_STORE_BYTES (3u * BATCH_STORE_SECTOR)
#define BATCH_STORE_DATA (2u * BATCH_STORE_SECTOR)

/* FETCHING means an incomplete batch must be fetched again from the Sensor.
 * READY means verified bytes can be re-uploaded with their original identity.
 * CONFIRMED is committed BEFORE reusing data space or advancing the Sensor. */
typedef enum {
    BATCH_IDLE, BATCH_FETCHING, BATCH_READY, BATCH_CONFIRMED
} batch_store_state_t;
typedef struct {
    uint32_t generation, total, offset, length;
    batch_store_state_t state;
    uint16_t data_crc;
    uint8_t log_sha256[32];
} batch_record_t;
typedef struct {
    void *context;
    const uint8_t *mapped;
    bool (*erase)(void *context, uint32_t offset);
    bool (*program)(void *context, uint32_t offset, const uint8_t page[256]);
} batch_flash_t;
typedef struct {
    batch_flash_t flash;
    batch_record_t record;
    unsigned journal_page;
} batch_store_t;

/* No boot erase. Invalid records are ignored only if an older valid one exists.
 * A damaged READY payload is reported, never silently accepted as an empty log. */
bool batch_store_open(batch_store_t *store, batch_flash_t flash);
bool batch_store_begin(batch_store_t *store, const uint8_t sha256[32],
                       uint32_t total, uint32_t offset);
bool batch_store_save(batch_store_t *store, const uint8_t *data, uint32_t length);
const uint8_t *batch_store_data(const batch_store_t *store);
bool batch_store_confirm(batch_store_t *store);
bool batch_store_release_confirmed(batch_store_t *store);
bool batch_store_finish(batch_store_t *store);
#endif
