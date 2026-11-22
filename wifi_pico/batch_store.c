/* Two append-only metadata sectors protect the last committed record while
 * rotating the journal. Hardware writes and cross-core safety live elsewhere. */
#include "batch_store.h"
#include "protocol/crc16.h"
#include "protocol/frame.h"
#include <string.h>

#define JOURNAL_PAGES (2u * BATCH_STORE_SECTOR / BATCH_STORE_PAGE)
#define PAGES_PER_SECTOR (BATCH_STORE_SECTOR / BATCH_STORE_PAGE)
#define NO_PAGE UINT32_MAX

static bool erased(const uint8_t *data, unsigned size)
{
    for (unsigned i = 0; i < size; ++i) if (data[i] != 0xff) return false;
    return true;
}
static bool valid_position(const batch_record_t *r)
{
    if (r->state > BATCH_CONFIRMED || r->total > BATCH_MAX_LOG_BYTES ||
        r->offset > r->total || r->offset % BATCH_BYTES) return false;
    uint32_t remaining = r->total - r->offset;
    uint32_t expected = remaining < BATCH_BYTES ? remaining : BATCH_BYTES;
    return r->length == expected && (!r->total || r->offset < r->total);
}
static void encode(const batch_record_t *r, uint8_t page[256])
{
    memset(page, 0xff, 256);
    memcpy(page, "UB41", 4); /* Format version 1, batch size 4 KiB. */
    write_u32_le(page + 4, r->generation);
    write_u32_le(page + 8, (uint32_t)r->state);
    write_u32_le(page + 12, r->total);
    write_u32_le(page + 16, r->offset);
    write_u32_le(page + 20, r->length);
    write_u16_le(page + 24, r->data_crc);
    memcpy(page + 32, r->log_sha256, 32);
    write_u16_le(page + 254, crc16_x25(page, 254));
}
static bool decode(const uint8_t page[256], batch_record_t *r)
{
    if (memcmp(page, "UB41", 4) ||
        read_u16_le(page + 254) != crc16_x25(page, 254)) return false;
    *r = (batch_record_t){
        .generation = read_u32_le(page + 4), .state = read_u32_le(page + 8),
        .total = read_u32_le(page + 12), .offset = read_u32_le(page + 16),
        .length = read_u32_le(page + 20), .data_crc = read_u16_le(page + 24)};
    memcpy(r->log_sha256, page + 32, 32);
    return r->generation && valid_position(r);
}
static bool commit(batch_store_t *s, batch_record_t next)
{
    if (s->record.generation == UINT32_MAX || !valid_position(&next)) return false;
    unsigned page = s->journal_page == NO_PAGE ? 0 : s->journal_page + 1;
    unsigned end = s->journal_page == NO_PAGE ? PAGES_PER_SECTOR :
        (s->journal_page / PAGES_PER_SECTOR + 1) * PAGES_PER_SECTOR;
    /* A torn write occupies its page; never try to program over it. */
    while (page < end && !erased(s->flash.mapped + page * 256, 256)) ++page;
    if (page == end) {
        page = s->journal_page < PAGES_PER_SECTOR ? PAGES_PER_SECTOR : 0;
        if (!s->flash.erase(s->flash.context, page * 256)) return false;
        if (!erased(s->flash.mapped + page * 256, BATCH_STORE_SECTOR)) return false;
    }
    uint8_t bytes[256];
    next.generation = s->record.generation + 1;
    encode(&next, bytes);
    if (!s->flash.program(s->flash.context, page * 256, bytes) ||
        memcmp(s->flash.mapped + page * 256, bytes, 256)) return false;
    s->record = next;
    s->journal_page = page;
    return true;
}
bool batch_store_open(batch_store_t *s, batch_flash_t flash)
{
    if (!s || !flash.mapped || !flash.erase || !flash.program) return false;
    *s = (batch_store_t){.flash = flash, .journal_page = NO_PAGE};
    bool dirty = false;
    for (unsigned page = 0; page < JOURNAL_PAGES; ++page) {
        const uint8_t *bytes = flash.mapped + page * 256;
        batch_record_t candidate;
        dirty |= !erased(bytes, 256);
        if (decode(bytes, &candidate) && candidate.generation > s->record.generation) {
            s->record = candidate;
            s->journal_page = page;
        }
    }
    if (s->journal_page == NO_PAGE &&
        (dirty || !erased(flash.mapped + BATCH_STORE_DATA, BATCH_BYTES))) return false;
    return s->record.state != BATCH_READY ||
        crc16_x25(flash.mapped + BATCH_STORE_DATA, s->record.length) == s->record.data_crc;
}
bool batch_store_begin(batch_store_t *s, const uint8_t sha256[32],
                       uint32_t total, uint32_t offset)
{
    if (!sha256 || s->record.state == BATCH_READY) return false;
    batch_record_t next = {.state = BATCH_FETCHING, .total = total, .offset = offset};
    if (offset > total) return false;
    next.length = total - offset < BATCH_BYTES ? total - offset : BATCH_BYTES;
    memcpy(next.log_sha256, sha256, 32);
    /* Commit the intent first: after a reset, partially written data is fetched
     * again, while the Sensor's original log remains the source of truth. */
    return commit(s, next) && s->flash.erase(s->flash.context, BATCH_STORE_DATA) &&
        erased(s->flash.mapped + BATCH_STORE_DATA, BATCH_BYTES);
}
bool batch_store_save(batch_store_t *s, const uint8_t *data, uint32_t length)
{
    if (s->record.state != BATCH_FETCHING || length != s->record.length ||
        (length && !data)) return false;
    for (uint32_t offset = 0; offset < length; offset += 256) {
        uint8_t page[256];
        memset(page, 0xff, sizeof(page));
        uint32_t count = length - offset < 256 ? length - offset : 256;
        memcpy(page, data + offset, count);
        if (!s->flash.program(s->flash.context, BATCH_STORE_DATA + offset, page)) return false;
    }
    if (length && memcmp(s->flash.mapped + BATCH_STORE_DATA, data, length)) return false;
    batch_record_t next = s->record;
    next.state = BATCH_READY;
    next.data_crc = crc16_x25(s->flash.mapped + BATCH_STORE_DATA, length);
    return commit(s, next);
}
const uint8_t *batch_store_data(const batch_store_t *s)
{
    return s->record.state == BATCH_READY ? s->flash.mapped + BATCH_STORE_DATA : NULL;
}
bool batch_store_confirm(batch_store_t *s)
{
    if (s->record.state != BATCH_READY) return false;
    batch_record_t next = s->record;
    next.state = BATCH_CONFIRMED;
    /* Only the caller's validated PC receipt may cause this transition.
     * Progress survives a power cut during the following data-sector erase. */
    return commit(s, next) && batch_store_release_confirmed(s);
}
bool batch_store_release_confirmed(batch_store_t *s)
{
    if (s->record.state != BATCH_CONFIRMED) return false;
    if (erased(s->flash.mapped + BATCH_STORE_DATA, BATCH_BYTES)) return true;
    return s->flash.erase(s->flash.context, BATCH_STORE_DATA) &&
        erased(s->flash.mapped + BATCH_STORE_DATA, BATCH_BYTES);
}
bool batch_store_finish(batch_store_t *s)
{
    if (s->record.state != BATCH_CONFIRMED ||
        s->record.offset + s->record.length != s->record.total) return false;
    batch_record_t next = s->record;
    next.state = BATCH_IDLE;
    return commit(s, next);
}
