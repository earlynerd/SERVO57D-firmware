#include "mks57d/pi_controller.h"

#include <math.h>
#include <stddef.h>

static bool finite_nonnegative(float value)
{
    return isfinite(value) && (value >= 0.0f);
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

bool pi_controller_config_is_valid(const pi_controller_config_t* config)
{
    return (config != NULL) &&
           finite_nonnegative(config->proportional_gain) &&
           finite_nonnegative(config->integral_gain_per_second) &&
           isfinite(config->output_limit) &&
           (config->output_limit > 0.0f) &&
           isfinite(config->integrator_limit) &&
           (config->integrator_limit > 0.0f);
}

void pi_controller_reset(pi_controller_t* controller)
{
    if (controller == NULL)
    {
        return;
    }

    controller->integrator = 0.0f;
    controller->initialized = true;
}

bool pi_controller_step(pi_controller_t* controller,
                        const pi_controller_config_t* config,
                        float error,
                        float elapsed_seconds,
                        float* output)
{
    float candidate_integrator;
    float candidate_output;
    bool saturating_high;
    bool saturating_low;

    if ((controller == NULL) || (output == NULL) ||
        !controller->initialized ||
        !pi_controller_config_is_valid(config) ||
        !isfinite(error) || !isfinite(elapsed_seconds) ||
        (elapsed_seconds <= 0.0f))
    {
        return false;
    }

    candidate_integrator = clamp_symmetric(
        controller->integrator +
        (config->integral_gain_per_second * error * elapsed_seconds),
        config->integrator_limit);
    candidate_output =
        (config->proportional_gain * error) + candidate_integrator;
    saturating_high = (candidate_output > config->output_limit) &&
                      (error > 0.0f);
    saturating_low = (candidate_output < -config->output_limit) &&
                     (error < 0.0f);

    if (!saturating_high && !saturating_low)
    {
        controller->integrator = candidate_integrator;
    }

    *output = clamp_symmetric(
        (config->proportional_gain * error) + controller->integrator,
        config->output_limit);
    return isfinite(*output);
}
