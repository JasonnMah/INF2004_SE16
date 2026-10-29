#ifndef WIFI_RX_RING_H
#define WIFI_RX_RING_H
#include "app.h"
#define RX_FAULT_OVERFLOW 1u
#define RX_FAULT_HARDWARE 2u
#define RX_FAULT_TX 4u
typedef struct {
    unsigned char data[APP_RX_CAPACITY];
    unsigned head, tail, fault;
    app_rx_stats_t stats;
} rx_ring_t;
/* Caller serializes task/ISR access. Both live on core 0 in this application. */
void rx_ring_init(rx_ring_t *ring);
void rx_ring_fault(rx_ring_t *ring, unsigned fault);
void rx_ring_push(rx_ring_t *ring, unsigned char byte);
unsigned rx_ring_take(rx_ring_t *ring, unsigned char *out, unsigned capacity, unsigned *fault);
#endif
