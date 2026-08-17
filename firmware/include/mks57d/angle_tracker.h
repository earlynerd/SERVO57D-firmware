#ifndef MKS57D_ANGLE_TRACKER_H
#define MKS57D_ANGLE_TRACKER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    uint32_t counts_per_revolution;
    uint32_t maximum_sample_interval_us;
    float maximum_velocity_revolutions_per_second;
    float velocity_filter_alpha;
} angle_tracker_config_t;

typedef struct
{
    angle_tracker_config_t config;
    uint32_t last_timestamp_us;
    uint16_t last_raw_angle;
    float position_revolutions;
    float velocity_revolutions_per_second;
    bool initialized;
} angle_tracker_t;

bool angle_tracker_config_is_valid(const angle_tracker_config_t* config);
bool angle_tracker_init(angle_tracker_t* tracker,
                        const angle_tracker_config_t* config);
bool angle_tracker_push(angle_tracker_t* tracker,
                        uint16_t raw_angle,
                        uint32_t timestamp_us);

#endif
