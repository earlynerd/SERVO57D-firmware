#ifndef MKS57D_TIMEBASE_RECONCILE_H
#define MKS57D_TIMEBASE_RECONCILE_H

#include <stdint.h>

/*
 * Reconcile a raw periodic-counter timestamp with the last published value.
 * A regression shorter than one counter period is the SysTick epoch-service
 * gap: the hardware counter has reloaded but its low-priority handler has not
 * published the next millisecond yet. Larger regressions remain clamped so an
 * unexpected stale sample cannot move the public clock backwards.
 *
 * All comparisons use modulo-2^32 time and therefore preserve the normal
 * uint32_t microsecond wrap, provided successive publications are less than
 * INT32_MAX microseconds apart.
 */
static inline uint32_t timebase_reconcile_microseconds(
    uint32_t previous_microseconds,
    uint32_t sampled_microseconds,
    uint32_t counter_period_microseconds)
{
    int32_t delta =
        (int32_t)(sampled_microseconds - previous_microseconds);

    if (delta < 0)
    {
        const uint32_t regression =
            previous_microseconds - sampled_microseconds;

        if ((counter_period_microseconds != 0u) &&
            (regression < counter_period_microseconds))
        {
            sampled_microseconds += counter_period_microseconds;
            delta = (int32_t)(sampled_microseconds -
                              previous_microseconds);
        }
    }

    return delta > 0 ? sampled_microseconds : previous_microseconds;
}

#endif
