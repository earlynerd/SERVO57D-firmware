#include "mks57d/servo_core.h"

#include <math.h>
#include <stddef.h>

static bool finite_positive(float value)
{
    return isfinite(value) && (value > 0.0f);
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

static void output_reset(servo_core_output_t* output, uint32_t fault_flags)
{
    output->valid = false;
    output->trajectory_settled = false;
    output->fault_flags = fault_flags;
    output->measured_position_revolutions = 0.0f;
    output->measured_velocity_revolutions_per_second = 0.0f;
    output->reference_position_revolutions = 0.0f;
    output->reference_velocity_revolutions_per_second = 0.0f;
    output->velocity_request_revolutions_per_second = 0.0f;
    output->torque_current_request_amperes = 0.0f;
}

static servo_core_status_t latch_fault(servo_core_t* core,
                                       uint32_t fault,
                                       servo_core_output_t* output)
{
    core->fault_flags |= fault;
    if (output != NULL)
    {
        output_reset(output, core->fault_flags);
    }
    return SERVO_CORE_STATUS_FAULTED;
}

bool servo_core_config_is_valid(const servo_core_config_t* config)
{
    return (config != NULL) &&
           angle_tracker_config_is_valid(&config->angle_tracker) &&
           motion_profile_config_is_valid(&config->motion_profile) &&
           pi_controller_config_is_valid(&config->velocity_controller) &&
           (config->encoder_stale_timeout_us != 0u) &&
           (config->maximum_control_interval_us != 0u) &&
           (config->maximum_control_interval_us <=
            config->encoder_stale_timeout_us) &&
           finite_positive(config->position_gain_per_second) &&
           finite_positive(config->maximum_following_error_revolutions) &&
           finite_positive(config->maximum_current_amperes);
}

bool servo_core_init(servo_core_t* core,
                     const servo_core_config_t* config)
{
    if ((core == NULL) || !servo_core_config_is_valid(config))
    {
        return false;
    }

    core->config = *config;
    if (!angle_tracker_init(&core->angle_tracker,
                            &config->angle_tracker))
    {
        return false;
    }
    pi_controller_reset(&core->velocity_controller);
    core->last_control_timestamp_us = 0u;
    core->fault_flags = SERVO_FAULT_NONE;
    core->initialized = true;
    core->feedback_ready = false;
    core->suspended = false;
    return true;
}

servo_core_status_t servo_core_observe_encoder(servo_core_t* core,
                                                uint16_t raw_angle,
                                                uint32_t timestamp_us)
{
    if ((core == NULL) || !core->initialized)
    {
        return SERVO_CORE_STATUS_INVALID_ARGUMENT;
    }
    if (core->fault_flags != SERVO_FAULT_NONE)
    {
        return SERVO_CORE_STATUS_FAULTED;
    }
    if (!angle_tracker_push(&core->angle_tracker,
                            raw_angle,
                            timestamp_us))
    {
        return latch_fault(core,
                           SERVO_FAULT_INVALID_ENCODER_SAMPLE,
                           NULL);
    }

    if (!core->feedback_ready)
    {
        if (!motion_profile_init(
                &core->motion_profile,
                core->angle_tracker.position_revolutions))
        {
            return latch_fault(core,
                               SERVO_FAULT_INTERNAL_NUMERIC,
                               NULL);
        }
        core->last_control_timestamp_us = timestamp_us;
        core->feedback_ready = true;
    }
    return SERVO_CORE_STATUS_OK;
}

servo_core_status_t servo_core_set_position_target(
    servo_core_t* core,
    float target_position_revolutions)
{
    if ((core == NULL) || !core->initialized ||
        !isfinite(target_position_revolutions))
    {
        return SERVO_CORE_STATUS_INVALID_ARGUMENT;
    }
    if (core->fault_flags != SERVO_FAULT_NONE)
    {
        return SERVO_CORE_STATUS_FAULTED;
    }
    if (!core->feedback_ready)
    {
        return SERVO_CORE_STATUS_NOT_READY;
    }
    if (!motion_profile_set_target(&core->motion_profile,
                                   target_position_revolutions))
    {
        return latch_fault(core, SERVO_FAULT_INTERNAL_NUMERIC, NULL);
    }
    return SERVO_CORE_STATUS_OK;
}

servo_core_status_t servo_core_request_stop(servo_core_t* core)
{
    if ((core == NULL) || !core->initialized)
    {
        return SERVO_CORE_STATUS_INVALID_ARGUMENT;
    }
    if (core->fault_flags != SERVO_FAULT_NONE)
    {
        return SERVO_CORE_STATUS_FAULTED;
    }
    if (!core->feedback_ready)
    {
        return SERVO_CORE_STATUS_NOT_READY;
    }
    if (!motion_profile_request_stop(&core->motion_profile,
                                     &core->config.motion_profile))
    {
        return latch_fault(core, SERVO_FAULT_INTERNAL_NUMERIC, NULL);
    }
    return SERVO_CORE_STATUS_OK;
}

servo_core_status_t servo_core_suspend(servo_core_t* core)
{
    if ((core == NULL) || !core->initialized)
    {
        return SERVO_CORE_STATUS_INVALID_ARGUMENT;
    }
    if (core->fault_flags != SERVO_FAULT_NONE)
    {
        return SERVO_CORE_STATUS_FAULTED;
    }
    if (!core->feedback_ready)
    {
        return SERVO_CORE_STATUS_NOT_READY;
    }
    if (!motion_profile_init(&core->motion_profile,
                             core->angle_tracker.position_revolutions))
    {
        return latch_fault(core, SERVO_FAULT_INTERNAL_NUMERIC, NULL);
    }
    pi_controller_reset(&core->velocity_controller);
    core->suspended = true;
    return SERVO_CORE_STATUS_OK;
}

servo_core_status_t servo_core_resume(servo_core_t* core,
                                      uint32_t timestamp_us)
{
    if ((core == NULL) || !core->initialized)
    {
        return SERVO_CORE_STATUS_INVALID_ARGUMENT;
    }
    if (core->fault_flags != SERVO_FAULT_NONE)
    {
        return SERVO_CORE_STATUS_FAULTED;
    }
    if (!core->feedback_ready)
    {
        return SERVO_CORE_STATUS_NOT_READY;
    }
    if (!motion_profile_init(&core->motion_profile,
                             core->angle_tracker.position_revolutions))
    {
        return latch_fault(core, SERVO_FAULT_INTERNAL_NUMERIC, NULL);
    }
    pi_controller_reset(&core->velocity_controller);
    core->last_control_timestamp_us = timestamp_us;
    core->suspended = false;
    return SERVO_CORE_STATUS_OK;
}

servo_core_status_t servo_core_step(servo_core_t* core,
                                    uint32_t timestamp_us,
                                    servo_core_output_t* output)
{
    uint32_t elapsed_us;
    uint32_t encoder_age_us;
    float elapsed_seconds;
    float following_error;
    float velocity_request;
    float current_request;

    if ((core == NULL) || (output == NULL) || !core->initialized)
    {
        return SERVO_CORE_STATUS_INVALID_ARGUMENT;
    }
    output_reset(output, core->fault_flags);
    if (core->fault_flags != SERVO_FAULT_NONE)
    {
        return SERVO_CORE_STATUS_FAULTED;
    }
    if (!core->feedback_ready)
    {
        return SERVO_CORE_STATUS_NOT_READY;
    }
    if (core->suspended)
    {
        return SERVO_CORE_STATUS_NOT_READY;
    }

    elapsed_us = timestamp_us - core->last_control_timestamp_us;
    if ((elapsed_us == 0u) ||
        (elapsed_us > core->config.maximum_control_interval_us))
    {
        return latch_fault(core,
                           SERVO_FAULT_CONTROL_DEADLINE,
                           output);
    }
    encoder_age_us =
        timestamp_us - core->angle_tracker.last_timestamp_us;
    if (encoder_age_us > core->config.encoder_stale_timeout_us)
    {
        return latch_fault(core, SERVO_FAULT_STALE_ENCODER, output);
    }

    elapsed_seconds = (float)elapsed_us * 1.0e-6f;
    if (!motion_profile_step(&core->motion_profile,
                             &core->config.motion_profile,
                             elapsed_seconds))
    {
        return latch_fault(core,
                           SERVO_FAULT_INTERNAL_NUMERIC,
                           output);
    }

    following_error =
        core->motion_profile.position_revolutions -
        core->angle_tracker.position_revolutions;
    if (!isfinite(following_error) ||
        (fabsf(following_error) >
         core->config.maximum_following_error_revolutions))
    {
        return latch_fault(core, SERVO_FAULT_FOLLOWING_ERROR, output);
    }

    velocity_request =
        core->motion_profile.velocity_revolutions_per_second +
        (core->config.position_gain_per_second * following_error);
    velocity_request = clamp_symmetric(
        velocity_request,
        core->config.motion_profile.maximum_velocity_revolutions_per_second);
    if (!pi_controller_step(
            &core->velocity_controller,
            &core->config.velocity_controller,
            velocity_request -
                core->angle_tracker.velocity_revolutions_per_second,
            elapsed_seconds,
            &current_request))
    {
        return latch_fault(core,
                           SERVO_FAULT_INTERNAL_NUMERIC,
                           output);
    }
    current_request = clamp_symmetric(
        current_request,
        core->config.maximum_current_amperes);

    core->last_control_timestamp_us = timestamp_us;
    output->valid = true;
    output->trajectory_settled = motion_profile_is_settled(
        &core->motion_profile,
        &core->config.motion_profile);
    output->fault_flags = core->fault_flags;
    output->measured_position_revolutions =
        core->angle_tracker.position_revolutions;
    output->measured_velocity_revolutions_per_second =
        core->angle_tracker.velocity_revolutions_per_second;
    output->reference_position_revolutions =
        core->motion_profile.position_revolutions;
    output->reference_velocity_revolutions_per_second =
        core->motion_profile.velocity_revolutions_per_second;
    output->velocity_request_revolutions_per_second = velocity_request;
    output->torque_current_request_amperes = current_request;
    return SERVO_CORE_STATUS_OK;
}

bool servo_core_is_faulted(const servo_core_t* core)
{
    return (core == NULL) ||
           (core->fault_flags != SERVO_FAULT_NONE);
}
