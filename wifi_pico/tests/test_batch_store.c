/* Host flash model only. It cannot establish physical SMP/XIP safety. */
#include "batch_store.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t memory[BATCH_STORE_BYTES], backup[BATCH_STORE_BYTES], data[BATCH_BYTES];
static unsigned calls, fail_at, torn;
static bool flash_erase(void *context, uint32_t offset)
{
    (void)context;
    assert(offset % 4096 == 0 && offset <= sizeof(memory) - 4096);
    ++calls;
    if (calls == fail_at) {
        if (torn) memset(memory + offset, 0xff, 1024);
        return false;
    }
    memset(memory + offset, 0xff, 4096);
    return true;
}
static bool flash_program(void *context, uint32_t offset, const uint8_t bytes[256])
{
    (void)context;
    assert(offset % 256 == 0 && offset <= sizeof(memory) - 256);
    unsigned count = ++calls == fail_at ? (torn ? 71 : 0) : 256;
    for (unsigned i = 0; i < count; ++i) {
        assert((memory[offset + i] & bytes[i]) == bytes[i]);
        memory[offset + i] &= bytes[i];
    }
    return count == 256;
}
static batch_flash_t flash(void)
{ return (batch_flash_t){.mapped = memory, .erase = flash_erase, .program = flash_program}; }
static void reset(void)
{ memset(memory, 0xff, sizeof(memory)); calls = fail_at = torn = 0; }
static void recover(batch_store_t *s)
{ assert(batch_store_open(s, flash())); }
int main(void)
{
    uint8_t identity[32] = {1};
    for (unsigned i = 0; i < sizeof(data); ++i) data[i] = (uint8_t)i;
    batch_store_t s;
    reset(); recover(&s);
    assert(s.record.state == BATCH_IDLE && calls == 0); /* Boot never erases. */
    assert(!batch_store_begin(&s, identity, BATCH_MAX_LOG_BYTES + 1, 0));
    assert(!batch_store_begin(&s, identity, 4096, 1));
    assert(batch_store_begin(&s, identity, 8192, 0));
    assert(batch_store_save(&s, data, sizeof(data)));
    assert(!batch_store_begin(&s, identity, 8192, 4096)); /* Protect pending bytes. */
    recover(&s);
    assert(s.record.state == BATCH_READY && s.record.offset == 0);
    assert(!memcmp(batch_store_data(&s), data, sizeof(data)));
    assert(!batch_store_finish(&s));
    assert(batch_store_confirm(&s)); recover(&s);
    assert(s.record.state == BATCH_CONFIRMED);
    assert(!batch_store_finish(&s)); /* This was not the last batch. */
    assert(batch_store_begin(&s, identity, 8192, 4096));
    assert(batch_store_save(&s, data, sizeof(data)));
    assert(batch_store_confirm(&s) && batch_store_finish(&s)); recover(&s);
    assert(s.record.state == BATCH_IDLE);
    puts("PASS cache: pending batch survives restart; confirmation precedes reuse; no boot erase");

    /* One full 1 MiB transfer crosses journal sectors repeatedly. */
    reset(); recover(&s);
    for (unsigned offset = 0; offset < BATCH_MAX_LOG_BYTES; offset += BATCH_BYTES) {
        assert(batch_store_begin(&s, identity, BATCH_MAX_LOG_BYTES, offset));
        assert(batch_store_save(&s, data, sizeof(data)));
        recover(&s); assert(s.record.offset == offset);
        assert(batch_store_confirm(&s)); recover(&s);
    }
    assert(batch_store_finish(&s)); recover(&s);
    assert(s.record.state == BATCH_IDLE && s.record.generation == 769);
    puts("PASS cache: all 256 batches of a 1 MiB log, including journal rotation");

    /* Cut every write/erase while replacing an already confirmed batch. */
    reset(); recover(&s);
    assert(batch_store_begin(&s, identity, 8192, 0));
    assert(batch_store_save(&s, data, sizeof(data)) && batch_store_confirm(&s));
    for (unsigned repeat = 0; repeat < 4; ++repeat) {
        assert(batch_store_begin(&s, identity, 8192, 0));
        assert(batch_store_save(&s, data, sizeof(data)) && batch_store_confirm(&s));
    }
    assert(s.record.generation == 15); /* Next save must rotate the journal. */
    memcpy(backup, memory, sizeof(memory));
    for (unsigned partial = 0; partial < 2; ++partial) {
        for (unsigned cut = 1; cut <= 22; ++cut) {
            memcpy(memory, backup, sizeof(memory)); calls = 0; fail_at = cut; torn = partial;
            recover(&s);
            if (batch_store_begin(&s, identity, 8192, 4096))
                (void)batch_store_save(&s, data, sizeof(data));
            fail_at = 0; recover(&s);
            assert(s.record.state == BATCH_CONFIRMED || s.record.state == BATCH_FETCHING ||
                   s.record.state == BATCH_READY);
            if (s.record.state == BATCH_READY) {
                assert(s.record.offset == 4096);
                assert(!memcmp(batch_store_data(&s), data, sizeof(data)));
            }
        }
    }
    puts("PASS cache: power-cut model at every operation; torn pages are never acknowledged as READY");

    /* A cut after receipt commit but during cleanup preserves delivery progress. */
    reset(); recover(&s);
    assert(batch_store_begin(&s, identity, 4096, 0));
    assert(batch_store_save(&s, data, sizeof(data)));
    calls = 0; fail_at = 2; torn = 1;
    assert(!batch_store_confirm(&s)); fail_at = 0; recover(&s);
    assert(s.record.state == BATCH_CONFIRMED);
    assert(batch_store_finish(&s));

    reset(); recover(&s);
    assert(batch_store_begin(&s, identity, 17, 0));
    assert(batch_store_save(&s, data, 17)); recover(&s);
    assert(s.record.length == 17 && !memcmp(batch_store_data(&s), data, 17));
    memory[BATCH_STORE_DATA] ^= 1;
    assert(!batch_store_open(&s, flash())); /* Never silently upload damage. */
    reset(); memory[0] = 0;
    assert(!batch_store_open(&s, flash()));
    reset(); memory[BATCH_STORE_DATA] = 0;
    assert(!batch_store_open(&s, flash())); /* Unknown data without metadata is protected. */
    reset(); recover(&s);
    assert(batch_store_begin(&s, identity, 0, 0) && batch_store_save(&s, NULL, 0));
    assert(batch_store_confirm(&s) && batch_store_finish(&s));
    puts("PASS cache: receipt/cleanup interruption, short/empty batches and corruption detection");
}
