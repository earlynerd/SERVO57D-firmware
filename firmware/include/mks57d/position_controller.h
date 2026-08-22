#ifndef MKS57D_POSITION_CONTROLLER_H
#define MKS57D_POSITION_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>

#include "mks57d/motion_profile.h"
#include "mks57d/rotor_observation.h"

typedef enum
{
    POSITION_CONTROL_STATE_IDLE = 0,
    POSITION_CONTROL_STATE_MOVING,
    POSITION_CONTROL_STATE_SETTLING,
    POSITION_CONTROL_STATE_COMPLETE,
    POSITION_CONTROL_STATE_STOPPED,
    POSITION_CONTROL_STATE_FAILED
} position_control_state_t;

typedef enum
{
    POSITION_CONTROL_RESULT_NONE = 0,
    POSITION_CONTROL_RESULT_SETTLED,
    POSITION_CONTROL_RESULT_DEADLINE,
    POSITION_CONTROL_RESULT_STOPPED,
    POSITION_CONTROL_RESULT_INVALID_FEEDBACK,
    POSITION_CONTROL_RESULT_FEEDBACK_TIMING,
    POSITION_CONTROL_RESULT_FOLLOWING_ERROR,
    POSITION_CONTROL_RESULT_INTERNAL_NUMERIC,
    POSITION_CONTROL_RESULT_ACTUATOR_FAULT
} position_control_result_t;

typedef enum
{
    POSITION_CONTROL_FAULT_NONE = 0u,
    POSITION_CONTROL_FAULT_INVALID_FEEDBACK = 1u << 0,
    POSITION_CONTROL_FAULT_FEEDBACK_TIMING = 1u << 1,
    POSITION_CONTROL_FAULT_FOLLOWING_ERROR = 1u << 2,
    POSITION_CONTROL_FAULT_INTERNAL_NUMERIC = 1u << 3,
    POSITION_CONTROL_FAULT_ACTUATOR = 1u << 4
} position_control_fault_t;

typedef enum
{
    POSITION_CONTROL_EVENT_NONE = 0,
    POSITION_CONTROL_EVENT_VELOCITY_CHANGED,
    POSITION_CONTROL_EVENT_COMPLETED,
    POSITION_CONTROL_EVENT_FAILED
} position_control_event_t;

typedef struct
{
    float maximum_relative_travel_revolutions;
    float maximum_velocity_revolutions_per_second;
    /* Inner velocity target headroom above the trajectory limit. */
    float maximum_velocity_target_revolutions_per_second;
    float maximum_acceleration_revolutions_per_second_squared;
    float maximum_feedback_velocity_revolutions_per_second;
    float maximum_start_velocity_revolutions_per_second;
    float maximum_following_error_revolutions;
    float position_gain_per_second;
    float position_tolerance_revolutions;
    float velocity_tolerance_revolutions_per_second;
    uint16_t maximum_current_counts;
    uint16_t required_settle_samples;
    uint32_t maximum_feedback_interval_us;
    uint32_t minimum_duration_millis;
    uint32_t maximum_duration_millis;
} position_controller_config_t;

typedef struct
{
    position_control_state_t state;
    position_control_result_t result;
    uint32_t fault_flags;
    int32_t requested_displacement_revolutions_q16_16;
    int32_t target_position_revolutions_q16_16;
    int32_t reference_position_revolutions_q16_16;
    int32_t measured_position_revolutions_q16_16;
    int32_t reference_velocity_revolutions_per_second_q16_16;
    int32_t target_velocity_revolutions_per_second_q16_16;
    int32_t measured_velocity_revolutions_per_second_q16_16;
    int32_t following_error_revolutions_q16_16;
    int32_t maximum_velocity_revolutions_per_second_q16_16;
    int32_t maximum_acceleration_revolutions_per_second2_q16_16;
    uint16_t current_limit_counts;
    uint16_t settle_sample_count;
    uint32_t elapsed_millis;
    bool active;
    bool profile_at_target;
    bool target_settled;
} position_controller_status_t;

typedef struct
{
    position_controller_config_t config;
    position_controller_status_t status;
    motion_profile_t profile;
    motion_profile_config_t active_profile_config;
    float target_position_revolutions;
    uint32_t start_millis;
    uint32_t deadline_millis;
    uint32_t last_feedback_timestamp_us;
    bool initialized;
} position_controller_t;

bool position_controller_config_is_valid(
    const position_controller_config_t* config);
bool position_controller_init(position_controller_t* controller,
                              const position_controller_config_t* config);
bool position_controller_start_relative(
    position_controller_t* controller,
    int32_t displacement_revolutions_q16_16,
    int32_t maximum_velocity_revolutions_per_second_q16_16,
    int32_t maximum_acceleration_revolutions_per_second2_q16_16,
    uint16_t current_limit_counts,
    uint32_t duration_millis,
    uint32_t now_millis,
    const rotor_observation_t* observation);
position_control_event_t position_controller_update(
    position_controller_t* controller,
    uint32_t now_millis,
    const rotor_observation_t* observation,
    int32_t* target_velocity_revolutions_per_second_q16_16);
bool position_controller_stop(position_controller_t* controller,
                              uint32_t now_millis);
bool position_controller_actuator_failed(position_controller_t* controller,
                                         uint32_t now_millis);
bool position_controller_is_active(const position_controller_t* controller);
void position_controller_get_status(
    const position_controller_t* controller,
    position_controller_status_t* status);

#endif
