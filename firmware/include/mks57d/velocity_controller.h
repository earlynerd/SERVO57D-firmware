#ifndef MKS57D_VELOCITY_CONTROLLER_H
#define MKS57D_VELOCITY_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>

#include "mks57d/pi_controller.h"
#include "mks57d/rotor_observation.h"

typedef enum
{
    VELOCITY_CONTROL_STATE_IDLE = 0,
    VELOCITY_CONTROL_STATE_RAMPING,
    VELOCITY_CONTROL_STATE_TRACKING,
    VELOCITY_CONTROL_STATE_COMPLETE,
    VELOCITY_CONTROL_STATE_STOPPED,
    VELOCITY_CONTROL_STATE_FAILED
} velocity_control_state_t;

typedef enum
{
    VELOCITY_CONTROL_RESULT_NONE = 0,
    VELOCITY_CONTROL_RESULT_DEADLINE,
    VELOCITY_CONTROL_RESULT_STOPPED,
    VELOCITY_CONTROL_RESULT_INVALID_FEEDBACK,
    VELOCITY_CONTROL_RESULT_FEEDBACK_TIMING,
    VELOCITY_CONTROL_RESULT_OVERSPEED,
    VELOCITY_CONTROL_RESULT_INTERNAL_NUMERIC,
    VELOCITY_CONTROL_RESULT_ACTUATOR_FAULT
} velocity_control_result_t;

typedef enum
{
    VELOCITY_CONTROL_EVENT_NONE = 0,
    VELOCITY_CONTROL_EVENT_CURRENT_CHANGED,
    VELOCITY_CONTROL_EVENT_COMPLETED,
    VELOCITY_CONTROL_EVENT_FAILED
} velocity_control_event_t;

enum
{
    VELOCITY_CONTROL_FAULT_INVALID_FEEDBACK = 1u << 0,
    VELOCITY_CONTROL_FAULT_FEEDBACK_TIMING = 1u << 1,
    VELOCITY_CONTROL_FAULT_OVERSPEED = 1u << 2,
    VELOCITY_CONTROL_FAULT_INTERNAL_NUMERIC = 1u << 3,
    VELOCITY_CONTROL_FAULT_ACTUATOR = 1u << 4
};

typedef struct
{
    pi_controller_config_t current_controller;
    float maximum_target_velocity_revolutions_per_second;
    float maximum_target_acceleration_revolutions_per_second_squared;
    float maximum_feedback_velocity_revolutions_per_second;
    uint16_t maximum_current_counts;
    uint16_t maximum_feedback_interval_us;
    uint32_t minimum_duration_millis;
    /* Must not exceed INT32_MAX for wrap-safe deadline comparisons. */
    uint32_t maximum_duration_millis;
} velocity_controller_config_t;

typedef struct
{
    velocity_control_state_t state;
    velocity_control_result_t result;
    uint32_t fault_flags;
    int32_t target_velocity_revolutions_per_second_q16_16;
    int32_t reference_velocity_revolutions_per_second_q16_16;
    int32_t measured_velocity_revolutions_per_second_q16_16;
    int32_t velocity_error_revolutions_per_second_q16_16;
    int16_t requested_q_current_counts;
    uint16_t current_limit_counts;
    uint32_t elapsed_millis;
    bool active;
} velocity_controller_status_t;

typedef struct
{
    velocity_controller_config_t config;
    velocity_controller_status_t status;
    pi_controller_t current_controller;
    float target_velocity_revolutions_per_second;
    float reference_velocity_revolutions_per_second;
    uint32_t start_millis;
    uint32_t deadline_millis;
    uint32_t last_feedback_timestamp_us;
    int8_t actuator_direction;
    bool initialized;
} velocity_controller_t;

bool velocity_controller_config_is_valid(
    const velocity_controller_config_t* config);
bool velocity_controller_init(
    velocity_controller_t* controller,
    const velocity_controller_config_t* config);
bool velocity_controller_start(
    velocity_controller_t* controller,
    int32_t target_velocity_revolutions_per_second_q16_16,
    uint16_t current_limit_counts,
    uint32_t duration_millis,
    int8_t actuator_direction,
    uint32_t now_millis,
    const rotor_observation_t* observation);
velocity_control_event_t velocity_controller_update(
    velocity_controller_t* controller,
    uint32_t now_millis,
    const rotor_observation_t* observation,
    int16_t* requested_q_current_counts);
bool velocity_controller_stop(
    velocity_controller_t* controller,
    uint32_t now_millis);
bool velocity_controller_actuator_failed(
    velocity_controller_t* controller,
    uint32_t now_millis);
bool velocity_controller_is_active(
    const velocity_controller_t* controller);
void velocity_controller_get_status(
    const velocity_controller_t* controller,
    velocity_controller_status_t* status);

#endif
