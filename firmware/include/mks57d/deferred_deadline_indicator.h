#ifndef MKS57D_DEFERRED_DEADLINE_INDICATOR_H
#define MKS57D_DEFERRED_DEADLINE_INDICATOR_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    uint32_t latest_started_sequence;
    uint32_t latest_completed_sequence;
    uint32_t latest_overdue_sequence;
    bool active;
} deferred_deadline_indicator_t;

static inline void deferred_deadline_indicator_init(
    volatile deferred_deadline_indicator_t* indicator)
{
    indicator->latest_started_sequence = 0u;
    indicator->latest_completed_sequence = 0u;
    indicator->latest_overdue_sequence = 0u;
    indicator->active = false;
}

static inline uint32_t deferred_deadline_indicator_start(
    volatile deferred_deadline_indicator_t* indicator)
{
    ++indicator->latest_started_sequence;
    return indicator->latest_started_sequence;
}

/* Called at the next nominal release boundary before starting another job.
 * A true result is an off-to-on transition, not a stretched indication. */
static inline bool deferred_deadline_indicator_deadline_elapsed(
    volatile deferred_deadline_indicator_t* indicator)
{
    if (indicator->latest_started_sequence ==
        indicator->latest_completed_sequence)
    {
        return false;
    }

    indicator->latest_overdue_sequence =
        indicator->latest_started_sequence;
    if (indicator->active)
    {
        return false;
    }

    indicator->active = true;
    return true;
}

/* A true result is an on-to-off transition. If several releases overlap,
 * the indication remains active until the newest overdue release completes. */
static inline bool deferred_deadline_indicator_complete(
    volatile deferred_deadline_indicator_t* indicator,
    uint32_t completed_sequence)
{
    indicator->latest_completed_sequence = completed_sequence;
    if (!indicator->active ||
        ((int32_t)(completed_sequence -
                   indicator->latest_overdue_sequence) < 0))
    {
        return false;
    }

    indicator->active = false;
    return true;
}

#endif
