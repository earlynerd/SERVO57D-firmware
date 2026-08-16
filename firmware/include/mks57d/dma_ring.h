#ifndef MKS57D_DMA_RING_H
#define MKS57D_DMA_RING_H

#include <stddef.h>
#include <stdint.h>

typedef struct
{
    uint32_t consumed_total;
    uint32_t overrun_count;
    uint32_t dropped_bytes;
} dma_ring_cursor_t;

void dma_ring_cursor_init(dma_ring_cursor_t* cursor,
                          uint32_t produced_total);

/*
 * Copy bytes which DMA has completed into foreground-owned storage. The DMA
 * producer is represented by a monotonically wrapping byte count. If the
 * producer has lapped the cursor, the oldest bytes are discarded explicitly
 * and accounted before any copy is made.
 */
size_t dma_ring_copy(dma_ring_cursor_t* cursor,
                     const volatile uint8_t* ring,
                     size_t ring_size,
                     uint32_t produced_total,
                     uint8_t* destination,
                     size_t destination_capacity);

#endif
