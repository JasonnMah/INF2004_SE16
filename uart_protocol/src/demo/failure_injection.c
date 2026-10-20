#include "failure_injection.h"
#include <string.h>

typedef struct
{
    bool corrupted_first_data;
    bool dropped_first_data_ack;
    bool sent_wrong_first_block;
    bool dropped_config_ack;

} failure_injection_t;

static failure_injection_t g_failure = {0};

static void (*debug_line)(const char *);

void failure_injection_reset(void (*log)(const char *))
{
    g_failure = (failure_injection_t){0};
    debug_line = log;
}

bool failure_send_data(const protocol_link_t *link, const pending_tx_t *pending,
                        uint16_t block, const uint8_t *payload, uint16_t length)
{
    // Other modes intentionally leave the initial DATA transmission untouched.
    (void)link;
    (void)pending;
    (void)block;
    (void)payload;
    (void)length;
    bool injected = false;

#if PROTOCOL_TEST == TEST_CORRUPT_FIRST_DATA
    if (block == 0 &&
        !g_failure.corrupted_first_data)
    {
        uint8_t corrupted[MAX_STUFFED_FRAME_SIZE];

        memcpy(corrupted, pending->frame, pending->frame_length);

        // Encoded byte 4 is the first Session ID byte (0x44 here),
        // safely away from the delimiters/escape sequence in this demo.
        // CRC is NOT recalculated, so the receiver must reject it.
        corrupted[4] ^= 0x01;

        uart_write_blocking(
            link->uart,
            corrupted,
            pending->frame_length
        );

        g_failure.corrupted_first_data = true;
        injected = true;

        protocol_log(link, "\n[TEST] Injected CRC corruption into first DATA frame\n");
    }
#endif

#if PROTOCOL_TEST == TEST_WRONG_FIRST_BLOCK
    if (block == 0 &&
        !g_failure.sent_wrong_first_block)
    {
        uint8_t wrong_payload[DATA_BLOCK_INDEX_SIZE + DATA_CHUNK_SIZE];
        uint8_t wrong_frame[MAX_STUFFED_FRAME_SIZE];

        // Send a CRC-valid frame claiming block 1 even though Pico 2
        // is expecting block 0. The retained pending frame remains correct.
        write_u16_le(wrong_payload, 1);

        memcpy(&wrong_payload[DATA_BLOCK_INDEX_SIZE], &payload[DATA_BLOCK_INDEX_SIZE],
               length - DATA_BLOCK_INDEX_SIZE);

        size_t wrong_length = frame_encode(
            MSG_DATA,
            0x00,
            link->session_id,
            pending->sequence,
            wrong_payload,
            length,
            wrong_frame,
            sizeof(wrong_frame)
        );

        if (wrong_length > 0)
        {
            uart_write_blocking(
                link->uart,
                wrong_frame,
                wrong_length
            );

            g_failure.sent_wrong_first_block = true;
            injected = true;

            protocol_log(link, "\n[TEST] Injected wrong block index in first DATA frame\n");
        }
    }
#endif

    return injected;
}

bool failure_drop_config_ack(void)
{
#if PROTOCOL_TEST == TEST_DROP_CONFIG_ACK
    if (!g_failure.dropped_config_ack)
    {
        g_failure.dropped_config_ack = true;
        debug_line("\n[TEST] Intentionally dropped first CONFIG ACK\n");
        return true;
    }
#endif
    return false;
}

bool failure_drop_data_ack(uint16_t block)
{
    (void)block;
#if PROTOCOL_TEST == TEST_DROP_FIRST_DATA_ACK
    if (block == 0 && !g_failure.dropped_first_data_ack)
    {
        g_failure.dropped_first_data_ack = true;
        debug_line("[TEST] Intentionally dropped ACK for DATA block 0\n");
        return true;
    }
#endif
    return false;
}

void failure_print_banner(void)
{
    debug_line("\n=== UART PROTOCOL FAILURE TEST START ===\n");

#if PROTOCOL_TEST == TEST_NONE
    debug_line("[TEST] Mode: NONE (normal happy path)\n");
#elif PROTOCOL_TEST == TEST_CORRUPT_FIRST_DATA
    debug_line("[TEST] Mode: CORRUPT FIRST DATA FRAME\n");
#elif PROTOCOL_TEST == TEST_DROP_FIRST_DATA_ACK
    debug_line("[TEST] Mode: DROP FIRST DATA ACK\n");
#elif PROTOCOL_TEST == TEST_WRONG_FIRST_BLOCK
    debug_line("[TEST] Mode: WRONG FIRST DATA BLOCK INDEX\n");
#elif PROTOCOL_TEST == TEST_DROP_CONFIG_ACK
    debug_line("[TEST] Mode: DROP FIRST CONFIG ACK\n");
#endif

}
