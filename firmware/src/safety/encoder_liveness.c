#include "mks57d/encoder_liveness.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

bool encoder_liveness_monitor_init(
    encoder_liveness_monitor_t* monitor,
    uint32_t maximum_stall_us)
{
    if ((monitor == NULL) || (maximum_stall_us == 0u) ||
        (maximum_stall_us > (uint32_t)INT32_MAX))
    {
        return false;
    }

    memset(monitor, 0, sizeof(*monitor));
    monitor->maximum_stall_us = maximum_stall_us;
    monitor->initialized = true;
    return true;
}

bool encoder_liveness_monitor_update(
    encoder_liveness_monitor_t* monitor,
    bool feedback_initialized,
    uint32_t sample_count,
    uint32_t sample_timestamp_us,
    uint32_t now_us)
{
    bool sample_advanced;

    if ((monitor == NULL) || !monitor->initialized)
    {
        return false;
    }

    sample_advanced = feedback_initialized &&
                      (!monitor->sample_seen ||
                       (sample_count != monitor->last_sample_count) ||
                       (sample_timestamp_us !=
                        monitor->last_sample_timestamp_us));
    if (sample_advanced)
    {
        monitor->last_sample_count = sample_count;
        monitor->last_sample_timestamp_us = sample_timestamp_us;
        monitor->sample_seen = true;
        monitor->live =
            ((now_us - sample_timestamp_us) <= monitor->maximum_stall_us);
    }
    else if (!feedback_initialized || !monitor->sample_seen)
    {
        monitor->live = false;
    }
    else if (monitor->live &&
             ((now_us - monitor->last_sample_timestamp_us) >
              monitor->maximum_stall_us))
    {
        monitor->live = false;
    }

    return monitor->live;
}

bool encoder_liveness_monitor_is_live(
    const encoder_liveness_monitor_t* monitor)
{
    return (monitor != NULL) && monitor->initialized && monitor->live;
}
