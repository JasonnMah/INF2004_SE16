#ifndef PROTOCOL_LINK_H
#define PROTOCOL_LINK_H
#include "protocol.h"
/* Platform owns UART and monotonic time. No SDK/kernel headers in the library. */
typedef struct {
    void *context;
    uint32_t session_id;
    void (*log)(const char *text);
    void (*write)(void *context, const uint8_t *bytes, size_t length);
    uint64_t (*now_ms)(void *context);
} protocol_link_t;
static inline bool protocol_link_valid(const protocol_link_t *link)
{ return link->write != NULL && link->now_ms != NULL; }
static inline void protocol_log(const protocol_link_t *link, const char *text)
{ if (link->log != NULL) { link->log(text); } }
static inline uint64_t protocol_now_ms(const protocol_link_t *link)
{ return link->now_ms(link->context); }
static inline void protocol_write(const protocol_link_t *link, const uint8_t *bytes, size_t length)
{ link->write(link->context, bytes, length); }
#endif
