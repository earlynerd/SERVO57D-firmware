#include "mks57d/velocity_controller.h"

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

static int16_t round_to_i16(float value)
{
    if (value >= (float)INT16_MAX)
    {
        return INT16_MAX;
    }
    if (value <= (float)INT16_MIN)
    {
        return INT16_MIN;
    }
    return (value >= 0.0f) ?
        (int16_t)(value + 0.5f) : (int16_t)(value - 0.5f);
}

static void clear_request(velocity_controller_t* controller)
{
    controller->status.requested_q_current_counts = 0;
    controller->status.active = false;
}

static velocity_control_event_t fail_controller(
    velocity_controller_t* controller,
    velocity_control_result_t result,
    uint32_t fault,
    uint32_t now_millis)
{
    controller->status.state = VELOCITY_CONTROL_STATE_FAILED;
    controller->status.result = result;
    controller->status.fault_flags |= fault;
    controller->status.elapsed_millis = now_millis - controller->start_millis;
    clear_request(controller);
    return VELOCITY_CONTROL_EVENT_FAILED;
}

bool velocity_controller_config_is_valid(
    const velocity_controller_config_t* config)
{
    return (config != NULL) &&
           pi_controller_config_is_valid(&config->current_controller) &&
           finite_positive(
               config->maximum_target_velocity_revolutions_per_second) &&
           finite_positive(
               config->maximum_target_acceleration_revolutions_per_second_squared) &&
           finite_positive(
               config->maximum_feedback_velocity_revolutions_per_second) &&
           (config->maximum_target_velocity_revolutions_per_second <=
            config->maximum_feedback_velocity_revolutions_per_second) &&
           (config->maximum_current_counts > 0u) &&
           (config->current_controller.output_limit >=
            (float)config->maximum_current_counts) &&
           (config->current_controller.integrator_limit <=
            config->current_controller.output_limit) &&
           (config->maximum_feedback_interval_us > 0u) &&
           (config->minimum_duration_millis > 0u) &&
           (config->maximum_duration_millis >=
            config->minimum_duration_millis) &&
           (config->maximum_duration_millis <= (uint32_t)INT32_MAX);
}

bool velocity_controller_init(
    velocity_controller_t* controller,
    const velocity_controller_config_t* config)
{
    if ((controller == NULL) || !velocity_controller_config_is_valid(config))
    {
        return false;
    }

    memset(controller, 0, sizeof(*controller));
    controller->config = *config;
    controller->status.state = VELOCITY_CONTROL_STATE_IDLE;
    pi_controller_reset(&controller->current_controller);
    controller->initialized = true;
    return true;
}

bool velocity_controller_start(
    velocity_controller_t* controller,
    int32_t target_velocity_revolutions_per_second_q16_16,
    uint16_t current_limit_counts,
    uint32_t duration_millis,
    uint32_t now_millis,
    const rotor_observation_t* observation)
{
    float target_velocity;

    if ((controller == NULL) || (observation == NULL) ||
        !controller->initialized || controller->status.active ||
        !observation->valid ||
        !isfinite(observation->position_revolutions) ||
        !isfinite(observation->velocity_revolutions_per_second) ||
        (target_velocity_revolutions_per_second_q16_16 == 0) ||
        (current_limit_counts == 0u) ||
        (current_limit_counts > controller->config.maximum_current_counts) ||
        (duration_millis < controller->config.minimum_duration_millis) ||
        (duration_millis > controller->config.maximum_duration_millis) ||
        (fabsf(observation->velocity_revolutions_per_second) >
         controller->config.maximum_feedback_velocity_revolutions_per_second))
    {
        return false;
    }
    target_velocity = q16_16_to_float(
        target_velocity_revolutions_per_second_q16_16);
    if (!isfinite(target_velocity) ||
        (fabsf(target_velocity) >
         controller->config.maximum_target_velocity_revolutions_per_second))
    {
        return false;
    }

    memset(&controller->status, 0, sizeof(controller->status));
    controller->status.state = VELOCITY_CONTROL_STATE_RAMPING;
    controller->status.target_velocity_revolutions_per_second_q16_16 =
        target_velocity_revolutions_per_second_q16_16;
    controller->status.reference_velocity_revolutions_per_second_q16_16 =
        float_to_q16_16(observation->velocity_revolutions_per_second);
    controller->status.measured_velocity_revolutions_per_second_q16_16 =
        float_to_q16_16(observation->velocity_revolutions_per_second);
    controller->status.current_limit_counts = current_limit_counts;
    controller->status.active = true;
    controller->target_velocity_revolutions_per_second = target_velocity;
    controller->reference_velocity_revolutions_per_second =
        observation->velocity_revolutions_per_second;
    controller->start_millis = now_millis;
    controller->deadline_millis = now_millis + duration_millis;
    controller->last_feedback_timestamp_us = observation->timestamp_us;
    pi_controller_reset(&controller->current_controller);
    return true;
}

velocity_control_event_t velocity_controller_update(
    velocity_controller_t* controller,
    uint32_t now_millis,
    const rotor_observation_t* observation,
    int16_t* requested_q_current_counts)
{
    uint32_t elapsed_us;
    float elapsed_seconds;
    float maximum_reference_delta;
    float remaining_reference_delta;
    float velocity_error;
    float current_request;
    pi_controller_config_t current_config;

    if (requested_q_current_counts != NULL)
    {
        *requested_q_current_counts = 0;
    }
    if ((controller == NULL) || (observation == NULL) ||
        (requested_q_current_counts == NULL) || !controller->initialized ||
        !controller->status.active)
    {
        return VELOCITY_CONTROL_EVENT_NONE;
    }

    controller->status.elapsed_millis = now_millis - controller->start_millis;
    if (!observation->valid ||
        !isfinite(observation->position_revolutions) ||
        !isfinite(observation->velocity_revolutions_per_second))
    {
        return fail_controller(
            controller,
            VELOCITY_CONTROL_RESULT_INVALID_FEEDBACK,
            VELOCITY_CONTROL_FAULT_INVALID_FEEDBACK,
            now_millis);
    }
    elapsed_us = observation->timestamp_us -
                 controller->last_feedback_timestamp_us;
    if ((elapsed_us == 0u) ||
        (elapsed_us > controller->config.maximum_feedback_interval_us))
    {
        return fail_controller(
            controller,
            VELOCITY_CONTROL_RESULT_FEEDBACK_TIMING,
            VELOCITY_CONTROL_FAULT_FEEDBACK_TIMING,
            now_millis);
    }
    if (fabsf(observation->velocity_revolutions_per_second) >
        controller->config.maximum_feedback_velocity_revolutions_per_second)
    {
        return fail_controller(
            controller,
            VELOCITY_CONTROL_RESULT_OVERSPEED,
            VELOCITY_CONTROL_FAULT_OVERSPEED,
            now_millis);
    }
    if ((int32_t)(now_millis - controller->deadline_millis) >= 0)
    {
        controller->status.state = VELOCITY_CONTROL_STATE_COMPLETE;
        controller->status.result = VELOCITY_CONTROL_RESULT_DEADLINE;
        clear_request(controller);
        return VELOCITY_CONTROL_EVENT_COMPLETED;
    }

    elapsed_seconds = (float)elapsed_us / (float)MICROSECONDS_PER_SECOND;
    maximum_reference_delta =
        controller->config.
            maximum_target_acceleration_revolutions_per_second_squared *
        elapsed_seconds;
    remaining_reference_delta =
        controller->target_velocity_revolutions_per_second -
        controller->reference_velocity_revolutions_per_second;
    if (fabsf(remaining_reference_delta) <= maximum_reference_delta)
    {
        controller->reference_velocity_revolutions_per_second =
            controller->target_velocity_revolutions_per_second;
        controller->status.state = VELOCITY_CONTROL_STATE_TRACKING;
    }
    else if (remaining_reference_delta > 0.0f)
    {
        controller->reference_velocity_revolutions_per_second +=
            maximum_reference_delta;
    }
    else
    {
        controller->reference_velocity_revolutions_per_second -=
            maximum_reference_delta;
    }

    velocity_error = controller->reference_velocity_revolutions_per_second -
                     observation->velocity_revolutions_per_second;
    current_config = controller->config.current_controller;
    current_config.output_limit = (float)controller->status.current_limit_counts;
    if (current_config.integrator_limit > current_config.output_limit)
    {
        current_config.integrator_limit = current_config.output_limit;
    }
    if (!isfinite(velocity_error) ||
        !pi_controller_step(
            &controller->current_controller,
            &current_config,
            velocity_error,
            elapsed_seconds,
            &current_request))
    {
        return fail_controller(
            controller,
            VELOCITY_CONTROL_RESULT_INTERNAL_NUMERIC,
            VELOCITY_CONTROL_FAULT_INTERNAL_NUMERIC,
            now_millis);
    }

    controller->status.reference_velocity_revolutions_per_second_q16_16 =
        float_to_q16_16(
            controller->reference_velocity_revolutions_per_second);
    controller->status.measured_velocity_revolutions_per_second_q16_16 =
        float_to_q16_16(observation->velocity_revolutions_per_second);
    controller->status.velocity_error_revolutions_per_second_q16_16 =
        float_to_q16_16(velocity_error);
    controller->status.requested_q_current_counts =
        round_to_i16(current_request);
    controller->last_feedback_timestamp_us = observation->timestamp_us;
    *requested_q_current_counts =
        controller->status.requested_q_current_counts;
    return VELOCITY_CONTROL_EVENT_CURRENT_CHANGED;
}

bool velocity_controller_stop(
    velocity_controller_t* controller,
    uint32_t now_millis)
{
    if ((controller == NULL) || !controller->initialized ||
        !controller->status.active)
    {
        return false;
    }

    controller->status.state = VELOCITY_CONTROL_STATE_STOPPED;
    controller->status.result = VELOCITY_CONTROL_RESULT_STOPPED;
    controller->status.elapsed_millis = now_millis - controller->start_millis;
    clear_request(controller);
    return true;
}

bool velocity_controller_actuator_failed(
    velocity_controller_t* controller,
    uint32_t now_millis)
{
    if ((controller == NULL) || !controller->initialized ||
        !controller->status.active)
    {
        return false;
    }
    (void)fail_controller(
        controller,
        VELOCITY_CONTROL_RESULT_ACTUATOR_FAULT,
        VELOCITY_CONTROL_FAULT_ACTUATOR,
        now_millis);
    return true;
}

bool velocity_controller_is_active(
    const velocity_controller_t* controller)
{
    return (controller != NULL) && controller->initialized &&
           controller->status.active;
}

void velocity_controller_get_status(
    const velocity_controller_t* controller,
    velocity_controller_status_t* status)
{
    const velocity_controller_status_t empty = {0};

    if (status == NULL)
    {
        return;
    }
    *status = ((controller != NULL) && controller->initialized) ?
        controller->status : empty;
}
