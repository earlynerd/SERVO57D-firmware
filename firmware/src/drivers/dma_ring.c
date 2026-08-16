#include "mks57d/dma_ring.h"

#include <stddef.h>
#include <stdint.h>

void dma_ring_cursor_init(dma_ring_cursor_t* cursor,
                          uint32_t produced_total)
{
    if (cursor == NULL)
    {
        return;
    }

    cursor->consumed_total = produced_total;
    cursor->overrun_count = 0u;
    cursor->dropped_bytes = 0u;
}

size_t dma_ring_copy(dma_ring_cursor_t* cursor,
                     const volatile uint8_t* ring,
                     size_t ring_size,
                     uint32_t produced_total,
                     uint8_t* destination,
                     size_t destination_capacity)
{
    uint32_t available;
    size_t copy_count;
    size_t index;
    size_t ring_index;

    if ((cursor == NULL) || (ring == NULL) || (ring_size == 0u) ||
        (ring_size > UINT32_MAX) ||
        ((destination == NULL) && (destination_capacity != 0u)))
    {
        return 0u;
    }

    available = produced_total - cursor->consumed_total;
    if (available > (uint32_t)ring_size)
    {
        const uint32_t dropped = available - (uint32_t)ring_size;

        cursor->consumed_total += dropped;
        cursor->dropped_bytes += dropped;
        ++cursor->overrun_count;
        available = (uint32_t)ring_size;
    }

    copy_count = (size_t)available;
    if (copy_count > destination_capacity)
    {
        copy_count = destination_capacity;
    }

    ring_index = (size_t)(cursor->consumed_total % (uint32_t)ring_size);
    for (index = 0u; index < copy_count; ++index)
    {
        destination[index] = ring[ring_index];
        ++cursor->consumed_total;
        ++ring_index;
        if (ring_index == ring_size)
        {
            ring_index = 0u;
        }
    }

    return copy_count;
}
