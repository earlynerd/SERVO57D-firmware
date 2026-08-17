#include "mks57d/current_controller.h"

#include <math.h>
#include <stddef.h>

static bool finite_vector(stationary_vector_t value)
{
    return isfinite(value.alpha) && isfinite(value.beta);
}

static bool finite_rotating_vector(rotating_vector_t value)
{
    return isfinite(value.d) && isfinite(value.q);
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

bool park_transform(stationary_vector_t stationary,
                    float electrical_angle_radians,
                    rotating_vector_t* rotating)
{
    float cosine;
    float sine;

    if ((rotating == NULL) || !finite_vector(stationary) ||
        !isfinite(electrical_angle_radians))
    {
        return false;
    }

    cosine = cosf(electrical_angle_radians);
    sine = sinf(electrical_angle_radians);
    rotating->d = (cosine * stationary.alpha) +
                  (sine * stationary.beta);
    rotating->q = (-sine * stationary.alpha) +
                  (cosine * stationary.beta);
    return finite_rotating_vector(*rotating);
}

bool inverse_park_transform(rotating_vector_t rotating,
                            float electrical_angle_radians,
                            stationary_vector_t* stationary)
{
    float cosine;
    float sine;

    if ((stationary == NULL) || !finite_rotating_vector(rotating) ||
        !isfinite(electrical_angle_radians))
    {
        return false;
    }

    cosine = cosf(electrical_angle_radians);
    sine = sinf(electrical_angle_radians);
    stationary->alpha = (cosine * rotating.d) -
                        (sine * rotating.q);
    stationary->beta = (sine * rotating.d) +
                       (cosine * rotating.q);
    return finite_vector(*stationary);
}

bool current_controller_config_is_valid(
    const current_controller_config_t* config)
{
    return (config != NULL) &&
           pi_controller_config_is_valid(&config->d_axis) &&
           pi_controller_config_is_valid(&config->q_axis) &&
           isfinite(config->maximum_voltage_magnitude) &&
           (config->maximum_voltage_magnitude > 0.0f) &&
           isfinite(config->vector_anti_windup_gain_per_second) &&
           (config->vector_anti_windup_gain_per_second >= 0.0f);
}

bool current_controller_init(current_controller_t* controller,
                             const current_controller_config_t* config)
{
    if ((controller == NULL) ||
        !current_controller_config_is_valid(config))
    {
        return false;
    }

    pi_controller_reset(&controller->d_axis);
    pi_controller_reset(&controller->q_axis);
    controller->initialized = true;
    return true;
}

bool current_controller_step(current_controller_t* controller,
                             const current_controller_config_t* config,
                             stationary_vector_t measured_current,
                             rotating_vector_t requested_current,
                             float electrical_angle_radians,
                             float elapsed_seconds,
                             current_controller_output_t* output)
{
    rotating_vector_t unconstrained_voltage;
    float voltage_magnitude;
    float vector_scale = 1.0f;

    if ((controller == NULL) || (output == NULL) ||
        !controller->initialized ||
        !current_controller_config_is_valid(config) ||
        !finite_vector(measured_current) ||
        !finite_rotating_vector(requested_current) ||
        !isfinite(electrical_angle_radians) ||
        !isfinite(elapsed_seconds) || (elapsed_seconds <= 0.0f))
    {
        return false;
    }

    if (!park_transform(measured_current,
                        electrical_angle_radians,
                        &output->measured_current) ||
        !pi_controller_step(
            &controller->d_axis,
            &config->d_axis,
            requested_current.d - output->measured_current.d,
            elapsed_seconds,
            &unconstrained_voltage.d) ||
        !pi_controller_step(
            &controller->q_axis,
            &config->q_axis,
            requested_current.q - output->measured_current.q,
            elapsed_seconds,
            &unconstrained_voltage.q))
    {
        return false;
    }

    voltage_magnitude = hypotf(unconstrained_voltage.d,
                               unconstrained_voltage.q);
    output->voltage_saturated =
        voltage_magnitude > config->maximum_voltage_magnitude;
    if (output->voltage_saturated)
    {
        vector_scale = config->maximum_voltage_magnitude /
                       voltage_magnitude;
    }
    output->voltage_dq.d = unconstrained_voltage.d * vector_scale;
    output->voltage_dq.q = unconstrained_voltage.q * vector_scale;

    if (output->voltage_saturated &&
        (config->vector_anti_windup_gain_per_second > 0.0f))
    {
        controller->d_axis.integrator = clamp_symmetric(
            controller->d_axis.integrator +
            (config->vector_anti_windup_gain_per_second *
             (output->voltage_dq.d - unconstrained_voltage.d) *
             elapsed_seconds),
            config->d_axis.integrator_limit);
        controller->q_axis.integrator = clamp_symmetric(
            controller->q_axis.integrator +
            (config->vector_anti_windup_gain_per_second *
             (output->voltage_dq.q - unconstrained_voltage.q) *
             elapsed_seconds),
            config->q_axis.integrator_limit);
    }

    return inverse_park_transform(output->voltage_dq,
                                  electrical_angle_radians,
                                  &output->voltage_alpha_beta);
}
