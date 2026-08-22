#include "mks57d/position_controller.h"

#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

enum
{
    Q16_SCALE = 1u << 16u,
    MICROSECONDS_PER_SECOND = 1000000u
};

static bool finite_positive(float value)
{
    return isfinite(value) && (value > 0.0f);
}

static float q16_16_to_float(int32_t value)
{
    return (float)value / (float)Q16_SCALE;
}

static int32_t float_to_q16_16(float value)
{
    const float maximum = 32767.9999847412109375f;

    if (value >= maximum)
    {
        return INT32_MAX;
    }
    if (value <= -32768.0f)
    {
        return INT32_MIN;
    }
    return (int32_t)(value * (float)Q16_SCALE);
}

static float clamp_symmetric(float value, float limit)
{
    if (value > limit)
    {
        return limit;
    }
    if (value < -limit)
    {
        return -limit;
    }
    return value;
}

static void clear_request(position_controller_t* controller)
{
    controller->status.active = false;
    controller->status.target_velocity_revolutions_per_second_q16_16 = 0;
}

static position_control_event_t fail_controller(
    position_controller_t* controller,
    position_control_result_t result,
    uint32_t fault,
    uint32_t now_millis)
{
    controller->status.state = POSITION_CONTROL_STATE_FAILED;
    controller->status.result = result;
    controller->status.fault_flags |= fault;
    controller->status.elapsed_millis = now_millis - controller->start_millis;
    clear_request(controller);
    return POSITION_CONTROL_EVENT_FAILED;
}

bool position_controller_config_is_valid(
    const position_controller_config_t* config)
{
    return (config != NULL) &&
           finite_positive(config->maximum_relative_travel_revolutions) &&
           finite_positive(config->maximum_velocity_revolutions_per_second) &&
           finite_positive(
               config->maximum_acceleration_revolutions_per_second_squared) &&
           finite_positive(
               config->maximum_feedback_velocity_revolutions_per_second) &&
           finite_positive(
               config->maximum_start_velocity_revolutions_per_second) &&
           (config->maximum_start_velocity_revolutions_per_second <=
            config->maximum_feedback_velocity_revolutions_per_second) &&
           (config->maximum_velocity_revolutions_per_second <
            config->maximum_feedback_velocity_revolutions_per_second) &&
           finite_positive(config->maximum_following_error_revolutions) &&
           finite_positive(config->position_gain_per_second) &&
           finite_positive(config->position_tolerance_revolutions) &&
           finite_positive(
               config->velocity_tolerance_revolutions_per_second) &&
           (config->maximum_current_counts > 0u) &&
           (config->required_settle_samples > 0u) &&
           (config->maximum_feedback_interval_us > 0u) &&
           (config->minimum_duration_millis > 0u) &&
           (config->maximum_duration_millis >=
            config->minimum_duration_millis) &&
           (config->maximum_duration_millis <= (uint32_t)INT32_MAX);
}

bool position_controller_init(position_controller_t* controller,
                              const position_controller_config_t* config)
{
    if ((controller == NULL) || !position_controller_config_is_valid(config))
    {
        return false;
    }

    memset(controller, 0, sizeof(*controller));
    controller->config = *config;
    controller->status.state = POSITION_CONTROL_STATE_IDLE;
    controller->status.result = POSITION_CONTROL_RESULT_NONE;
    controller->initialized = true;
    return true;
}

bool position_controller_start_relative(
    position_controller_t* controller,
    int32_t displacement_revolutions_q16_16,
    int32_t maximum_velocity_revolutions_per_second_q16_16,
    int32_t maximum_acceleration_revolutions_per_second2_q16_16,
    uint16_t current_limit_counts,
    uint32_t duration_millis,
    uint32_t now_millis,
    const rotor_observation_t* observation)
{
    float displacement;
    float maximum_velocity;
    float maximum_acceleration;
    float target_position;

    if ((controller == NULL) || (observation == NULL) ||
        !controller->initialized || controller->status.active ||
        !observation->valid ||
        !isfinite(observation->position_revolutions) ||
        !isfinite(observation->velocity_revolutions_per_second) ||
        (displacement_revolutions_q16_16 == 0) ||
        (maximum_velocity_revolutions_per_second_q16_16 <= 0) ||
        (maximum_acceleration_revolutions_per_second2_q16_16 <= 0) ||
        (current_limit_counts == 0u) ||
        (current_limit_counts > controller->config.maximum_current_counts) ||
        (duration_millis < controller->config.minimum_duration_millis) ||
        (duration_millis > controller->config.maximum_duration_millis) ||
        (fabsf(observation->velocity_revolutions_per_second) >
         controller->config.maximum_start_velocity_revolutions_per_second))
    {
        return false;
    }

    displacement = q16_16_to_float(displacement_revolutions_q16_16);
    maximum_velocity = q16_16_to_float(
        maximum_velocity_revolutions_per_second_q16_16);
    maximum_acceleration = q16_16_to_float(
        maximum_acceleration_revolutions_per_second2_q16_16);
    target_position = observation->position_revolutions + displacement;
    if (!isfinite(displacement) ||
        (fabsf(displacement) >
         controller->config.maximum_relative_travel_revolutions) ||
        !finite_positive(maximum_velocity) ||
        (maximum_velocity >
         controller->config.maximum_velocity_revolutions_per_second) ||
        !finite_positive(maximum_acceleration) ||
        (maximum_acceleration >
         controller->config.
             maximum_acceleration_revolutions_per_second_squared) ||
        !isfinite(target_position) ||
        (target_position < -32768.0f) ||
        (target_position >= 32768.0f))
    {
        return false;
    }

    memset(&controller->status, 0, sizeof(controller->status));
    controller->active_profile_config.
        maximum_velocity_revolutions_per_second = maximum_velocity;
    controller->active_profile_config.
        maximum_acceleration_revolutions_per_second_squared =
            maximum_acceleration;
    controller->active_profile_config.maximum_step_seconds =
        (float)controller->config.maximum_feedback_interval_us /
        (float)MICROSECONDS_PER_SECOND;
    controller->active_profile_config.position_tolerance_revolutions =
        controller->config.position_tolerance_revolutions;
    controller->active_profile_config.
        velocity_tolerance_revolutions_per_second =
            controller->config.velocity_tolerance_revolutions_per_second;
    if (!motion_profile_init(
            &controller->profile, observation->position_revolutions) ||
        !motion_profile_set_target(&controller->profile, target_position))
    {
        return false;
    }
    controller->profile.velocity_revolutions_per_second =
        observation->velocity_revolutions_per_second;
    controller->target_position_revolutions = target_position;
    controller->start_millis = now_millis;
    controller->deadline_millis = now_millis + duration_millis;
    controller->last_feedback_timestamp_us = observation->timestamp_us;
    controller->status.state = POSITION_CONTROL_STATE_MOVING;
    controller->status.result = POSITION_CONTROL_RESULT_NONE;
    controller->status.requested_displacement_revolutions_q16_16 =
        displacement_revolutions_q16_16;
    controller->status.target_position_revolutions_q16_16 =
        float_to_q16_16(target_position);
    controller->status.reference_position_revolutions_q16_16 =
        float_to_q16_16(observation->position_revolutions);
    controller->status.measured_position_revolutions_q16_16 =
        float_to_q16_16(observation->position_revolutions);
    controller->status.reference_velocity_revolutions_per_second_q16_16 =
        float_to_q16_16(observation->velocity_revolutions_per_second);
    controller->status.measured_velocity_revolutions_per_second_q16_16 =
        float_to_q16_16(observation->velocity_revolutions_per_second);
    controller->status.maximum_velocity_revolutions_per_second_q16_16 =
        maximum_velocity_revolutions_per_second_q16_16;
    controller->status.
        maximum_acceleration_revolutions_per_second2_q16_16 =
            maximum_acceleration_revolutions_per_second2_q16_16;
    controller->status.current_limit_counts = current_limit_counts;
    controller->status.active = true;
    return true;
}

position_control_event_t position_controller_update(
    position_controller_t* controller,
    uint32_t now_millis,
    const rotor_observation_t* observation,
    int32_t* target_velocity_revolutions_per_second_q16_16)
{
    uint32_t elapsed_us;
    float elapsed_seconds;
    float following_error;
    float target_error;
    float velocity_target;
    bool profile_at_target;
    bool target_settled;

    if ((controller == NULL) || (observation == NULL) ||
        (target_velocity_revolutions_per_second_q16_16 == NULL) ||
        !controller->initialized || !controller->status.active)
    {
        return POSITION_CONTROL_EVENT_NONE;
    }
    *target_velocity_revolutions_per_second_q16_16 = 0;
    if (!observation->valid ||
        !isfinite(observation->position_revolutions) ||
        !isfinite(observation->velocity_revolutions_per_second))
    {
        return fail_controller(
            controller,
            POSITION_CONTROL_RESULT_INVALID_FEEDBACK,
            POSITION_CONTROL_FAULT_INVALID_FEEDBACK,
            now_millis);
    }
    elapsed_us = observation->timestamp_us -
                 controller->last_feedback_timestamp_us;
    if ((elapsed_us == 0u) ||
        (elapsed_us > controller->config.maximum_feedback_interval_us))
    {
        return fail_controller(
            controller,
            POSITION_CONTROL_RESULT_FEEDBACK_TIMING,
            POSITION_CONTROL_FAULT_FEEDBACK_TIMING,
            now_millis);
    }
    if (fabsf(observation->velocity_revolutions_per_second) >
        controller->config.maximum_feedback_velocity_revolutions_per_second)
    {
        return fail_controller(
            controller,
            POSITION_CONTROL_RESULT_INVALID_FEEDBACK,
            POSITION_CONTROL_FAULT_INVALID_FEEDBACK,
            now_millis);
    }
    if ((int32_t)(now_millis - controller->deadline_millis) >= 0)
    {
        controller->status.state = POSITION_CONTROL_STATE_COMPLETE;
        controller->status.result = POSITION_CONTROL_RESULT_DEADLINE;
        controller->status.elapsed_millis =
            now_millis - controller->start_millis;
        clear_request(controller);
        return POSITION_CONTROL_EVENT_COMPLETED;
    }

    elapsed_seconds = (float)elapsed_us / (float)MICROSECONDS_PER_SECOND;
    if (!motion_profile_step(
            &controller->profile,
            &controller->active_profile_config,
            elapsed_seconds))
    {
        return fail_controller(
            controller,
            POSITION_CONTROL_RESULT_INTERNAL_NUMERIC,
            POSITION_CONTROL_FAULT_INTERNAL_NUMERIC,
            now_millis);
    }

    following_error = controller->profile.position_revolutions -
                      observation->position_revolutions;
    if (!isfinite(following_error) ||
        (fabsf(following_error) >
         controller->config.maximum_following_error_revolutions))
    {
        return fail_controller(
            controller,
            POSITION_CONTROL_RESULT_FOLLOWING_ERROR,
            POSITION_CONTROL_FAULT_FOLLOWING_ERROR,
            now_millis);
    }
    velocity_target =
        controller->profile.velocity_revolutions_per_second +
        controller->config.position_gain_per_second * following_error;
    velocity_target = clamp_symmetric(
        velocity_target,
        controller->active_profile_config.
            maximum_velocity_revolutions_per_second);
    if (!isfinite(velocity_target))
    {
        return fail_controller(
            controller,
            POSITION_CONTROL_RESULT_INTERNAL_NUMERIC,
            POSITION_CONTROL_FAULT_INTERNAL_NUMERIC,
            now_millis);
    }

    target_error = controller->target_position_revolutions -
                   observation->position_revolutions;
    profile_at_target = motion_profile_is_settled(
        &controller->profile, &controller->active_profile_config);
    target_settled = profile_at_target &&
        (fabsf(target_error) <=
         controller->config.position_tolerance_revolutions) &&
        (fabsf(observation->velocity_revolutions_per_second) <=
         controller->config.velocity_tolerance_revolutions_per_second);
    if (target_settled)
    {
        if (controller->status.settle_sample_count < UINT16_MAX)
        {
            ++controller->status.settle_sample_count;
        }
    }
    else
    {
        controller->status.settle_sample_count = 0u;
    }

    controller->status.state = profile_at_target ?
        POSITION_CONTROL_STATE_SETTLING : POSITION_CONTROL_STATE_MOVING;
    controller->status.reference_position_revolutions_q16_16 =
        float_to_q16_16(controller->profile.position_revolutions);
    controller->status.measured_position_revolutions_q16_16 =
        float_to_q16_16(observation->position_revolutions);
    controller->status.reference_velocity_revolutions_per_second_q16_16 =
        float_to_q16_16(
            controller->profile.velocity_revolutions_per_second);
    controller->status.target_velocity_revolutions_per_second_q16_16 =
        float_to_q16_16(velocity_target);
    controller->status.measured_velocity_revolutions_per_second_q16_16 =
        float_to_q16_16(observation->velocity_revolutions_per_second);
    controller->status.following_error_revolutions_q16_16 =
        float_to_q16_16(following_error);
    controller->status.elapsed_millis = now_millis - controller->start_millis;
    controller->status.profile_at_target = profile_at_target;
    controller->status.target_settled = target_settled;
    controller->last_feedback_timestamp_us = observation->timestamp_us;

    if (controller->status.settle_sample_count >=
        controller->config.required_settle_samples)
    {
        controller->status.state = POSITION_CONTROL_STATE_COMPLETE;
        controller->status.result = POSITION_CONTROL_RESULT_SETTLED;
        clear_request(controller);
        return POSITION_CONTROL_EVENT_COMPLETED;
    }

    *target_velocity_revolutions_per_second_q16_16 =
        controller->status.target_velocity_revolutions_per_second_q16_16;
    return POSITION_CONTROL_EVENT_VELOCITY_CHANGED;
}

bool position_controller_stop(position_controller_t* controller,
                              uint32_t now_millis)
{
    if ((controller == NULL) || !controller->initialized ||
        !controller->status.active)
    {
        return false;
    }
    controller->status.state = POSITION_CONTROL_STATE_STOPPED;
    controller->status.result = POSITION_CONTROL_RESULT_STOPPED;
    controller->status.elapsed_millis = now_millis - controller->start_millis;
    clear_request(controller);
    return true;
}

bool position_controller_actuator_failed(position_controller_t* controller,
                                         uint32_t now_millis)
{
    if ((controller == NULL) || !controller->initialized ||
        !controller->status.active)
    {
        return false;
    }
    (void)fail_controller(
        controller,
        POSITION_CONTROL_RESULT_ACTUATOR_FAULT,
        POSITION_CONTROL_FAULT_ACTUATOR,
        now_millis);
    return true;
}

bool position_controller_is_active(const position_controller_t* controller)
{
    return (controller != NULL) && controller->initialized &&
           controller->status.active;
}

void position_controller_get_status(
    const position_controller_t* controller,
    position_controller_status_t* status)
{
    if ((controller == NULL) || (status == NULL))
    {
        return;
    }
    *status = controller->status;
}
