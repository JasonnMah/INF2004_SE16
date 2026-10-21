#include "rx_ring.h"
_Static_assert((APP_RX_CAPACITY & (APP_RX_CAPACITY - 1u)) == 0, "Ring must be power of two");
void rx_ring_init(rx_ring_t *ring) { *ring = (rx_ring_t){0}; }
void rx_ring_fault(rx_ring_t *ring, unsigned fault)
{
    if (!ring->fault) { ++ring->stats.faults; }
    ring->fault |= fault;
}
void rx_ring_push(rx_ring_t *ring, unsigned char byte)
{
    ++ring->stats.bytes_seen;
    unsigned next = (ring->head + 1u) & (APP_RX_CAPACITY - 1u);
    if (ring->fault) { ++ring->stats.dropped; return; }
    if (next == ring->tail) {
        ++ring->stats.dropped;
        rx_ring_fault(ring, RX_FAULT_OVERFLOW);
        return;
    }
    ring->data[ring->head] = byte;
    ring->head = next;
}
unsigned rx_ring_take(rx_ring_t *ring, unsigned char *out, unsigned capacity, unsigned *fault)
{
    *fault = ring->fault;
    if (*fault) {
        /* Do not join bytes from opposite sides of a loss into a valid frame. */
        ring->tail = ring->head;
        ring->fault = 0;
        return 0;
    }
    unsigned count = 0;
    while (count < capacity && ring->tail != ring->head) {
        out[count++] = ring->data[ring->tail];
        ring->tail = (ring->tail + 1u) & (APP_RX_CAPACITY - 1u);
    }
    return count;
}
