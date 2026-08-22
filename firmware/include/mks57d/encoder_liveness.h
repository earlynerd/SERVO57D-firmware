#ifndef MKS57D_ENCODER_LIVENESS_H
#define MKS57D_ENCODER_LIVENESS_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    uint32_t maximum_stall_us;
    uint32_t last_sample_count;
    uint32_t last_sample_timestamp_us;
    bool sample_seen;
    bool live;
    bool initialized;
} encoder_liveness_monitor_t;

bool encoder_liveness_monitor_init(
    encoder_liveness_monitor_t* monitor,
    uint32_t maximum_stall_us);

bool encoder_liveness_monitor_update(
    encoder_liveness_monitor_t* monitor,
    bool feedback_initialized,
    uint32_t sample_count,
    uint32_t sample_timestamp_us,
    uint32_t now_us);

bool encoder_liveness_monitor_is_live(
    const encoder_liveness_monitor_t* monitor);

#endif
