#include "mks57d/fault_latch.h"

#include <stddef.h>

void fault_latch_init(fault_latch_t *latch)
{
    if (latch == NULL)
    {
        return;
    }

    latch->active = 0u;
    latch->first = FAULT_SOURCE_NONE;
}

void fault_latch_raise(fault_latch_t *latch, fault_source_t source)
{
    if ((latch == NULL) || (source == FAULT_SOURCE_NONE))
    {
        return;
    }

    if (latch->first == FAULT_SOURCE_NONE)
    {
        latch->first = source;
    }

    latch->active |= (uint32_t)source;
}

bool fault_latch_is_active(const fault_latch_t *latch)
{
    return (latch != NULL) && (latch->active != 0u);
}
