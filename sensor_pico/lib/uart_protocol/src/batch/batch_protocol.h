#ifndef UART_BATCH_PROTOCOL_H
#define UART_BATCH_PROTOCOL_H
#include "protocol/frame.h"
#define UART_BATCH_BYTES 4096u
#define UART_BATCH_MAX_LOG (1024u * 1024u)
#define UART_BATCH_CONTROL_MS 5000u
#define UART_BATCH_UPLOAD_ACK_MS 120000u
#define UART_BATCH_OPEN_SIZE 44u
#define UART_BATCH_INFO_SIZE 48u
#define UART_BATCH_DONE_SIZE 36u
/* Opt-in extension. Legacy message IDs, DATA layout, stuffing and CRC stay
 * unchanged. A new data session and local block index are used for each range. */
enum { MSG_BATCH_OPEN = 0x09, MSG_BATCH_INFO = 0x0a, MSG_BATCH_DONE = 0x0b };
typedef struct {
    uint8_t sha256[32];
    uint32_t total, offset, session;
    uint16_t length, crc;
} batch_info_t;
static inline bool batch_digest_zero(const uint8_t *digest)
{
    for (unsigned i = 0; i < 32; ++i) if (digest[i]) return false;
    return true;
}
static inline bool batch_info_valid(const batch_info_t *info)
{
    if (!info->session || info->total > UART_BATCH_MAX_LOG || info->offset > info->total ||
        info->offset % UART_BATCH_BYTES || (info->total && info->offset == info->total)) return false;
    uint32_t remaining = info->total - info->offset;
    return info->length == (remaining < UART_BATCH_BYTES ? remaining : UART_BATCH_BYTES);
}
static inline void batch_pack(const batch_info_t *info, uint8_t *payload, bool response)
{
    for (unsigned i = 0; i < 32; ++i) payload[i] = info->sha256[i];
    write_u32_le(payload + 32, info->total);
    write_u32_le(payload + 36, info->offset);
    write_u32_le(payload + 40, info->session);
    if (response) {
        write_u16_le(payload + 44, info->length);
        write_u16_le(payload + 46, info->crc);
    }
}
static inline void batch_unpack(batch_info_t *info, const uint8_t *payload, bool response)
{
    for (unsigned i = 0; i < 32; ++i) info->sha256[i] = payload[i];
    info->total = read_u32_le(payload + 32);
    info->offset = read_u32_le(payload + 36);
    info->session = read_u32_le(payload + 40);
    info->length = response ? read_u16_le(payload + 44) : 0;
    info->crc = response ? read_u16_le(payload + 46) : 0;
}
#endif
