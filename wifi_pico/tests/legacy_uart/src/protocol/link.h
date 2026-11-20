#ifndef PROTOCOL_LINK_H
#define PROTOCOL_LINK_H

#include "protocol.h"
#include "hardware/uart.h"

// The application chooses the UART, session and optional diagnostic sink.
typedef struct
{
    uart_inst_t *uart;
    uint32_t session_id;
    void (*log)(const char *text);
} protocol_link_t;

static inline void protocol_log(const protocol_link_t *link, const char *text)
{
    if (link->log != NULL)
    {
        link->log(text);
    }
}

#endif
