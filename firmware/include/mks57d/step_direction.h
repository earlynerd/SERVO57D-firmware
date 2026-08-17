#ifndef MKS57D_STEP_DIRECTION_H
#define MKS57D_STEP_DIRECTION_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    STEP_DIRECTION_EVENT_NONE = 0,
    STEP_DIRECTION_EVENT_ENABLED,
    STEP_DIRECTION_EVENT_DISABLED,
    STEP_DIRECTION_EVENT_TARGET_UPDATED
} step_direction_event_t;

typedef struct
{
    uint32_t steps_per_revolution;
    uint32_t maximum_sample_interval_us;
    float maximum_step_rate_per_second;
} step_direction_config_t;

typedef struct
{
    step_direction_config_t config;
    int32_t last_cumulative_steps;
    uint32_t last_timestamp_us;
    float target_position_revolutions;
    bool enabled;
    bool initialized;
} step_direction_t;

typedef struct
{
    step_direction_event_t event;
    float target_position_revolutions;
    int32_t delta_steps;
} step_direction_output_t;

bool step_direction_config_is_valid(const step_direction_config_t* config);
bool step_direction_init(step_direction_t* model,
                         const step_direction_config_t* config);
bool step_direction_update(step_direction_t* model,
                           int32_t cumulative_steps,
                           bool enabled,
                           uint32_t timestamp_us,
                           float current_position_revolutions,
                           step_direction_output_t* output);

#endif
