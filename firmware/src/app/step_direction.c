#include "mks57d/step_direction.h"

#include <math.h>
#include <stddef.h>

static bool cumulative_delta(int32_t current,
                             int32_t previous,
                             int32_t* delta)
{
    const uint32_t raw_delta =
        (uint32_t)current - (uint32_t)previous;

    if (delta == NULL)
    {
        return false;
    }
    if (raw_delta < 0x80000000u)
    {
        *delta = (int32_t)raw_delta;
        return true;
    }
    if (raw_delta == 0x80000000u)
    {
        return false;
    }

    *delta = -(int32_t)(0u - raw_delta);
    return true;
}

bool step_direction_config_is_valid(const step_direction_config_t* config)
{
    return (config != NULL) &&
           (config->steps_per_revolution != 0u) &&
           (config->maximum_sample_interval_us != 0u) &&
           isfinite(config->maximum_step_rate_per_second) &&
           (config->maximum_step_rate_per_second > 0.0f);
}

bool step_direction_init(step_direction_t* model,
                         const step_direction_config_t* config)
{
    if ((model == NULL) || !step_direction_config_is_valid(config))
    {
        return false;
    }

    model->config = *config;
    model->last_cumulative_steps = 0;
    model->last_timestamp_us = 0u;
    model->target_position_revolutions = 0.0f;
    model->enabled = false;
    model->initialized = false;
    return true;
}

bool step_direction_update(step_direction_t* model,
                           int32_t cumulative_steps,
                           bool enabled,
                           uint32_t timestamp_us,
                           float current_position_revolutions,
                           step_direction_output_t* output)
{
    uint32_t elapsed_us;
    int32_t delta_steps;
    float allowed_steps;

    if ((model == NULL) || (output == NULL) ||
        !step_direction_config_is_valid(&model->config) ||
        !isfinite(current_position_revolutions))
    {
        return false;
    }

    output->event = STEP_DIRECTION_EVENT_NONE;
    output->target_position_revolutions =
        model->target_position_revolutions;
    output->delta_steps = 0;

    if (!model->initialized)
    {
        model->last_cumulative_steps = cumulative_steps;
        model->last_timestamp_us = timestamp_us;
        model->target_position_revolutions =
            current_position_revolutions;
        model->enabled = enabled;
        model->initialized = true;
        output->event = enabled ? STEP_DIRECTION_EVENT_ENABLED :
                                  STEP_DIRECTION_EVENT_NONE;
        output->target_position_revolutions =
            model->target_position_revolutions;
        return true;
    }

    if (enabled != model->enabled)
    {
        model->last_cumulative_steps = cumulative_steps;
        model->last_timestamp_us = timestamp_us;
        model->target_position_revolutions =
            current_position_revolutions;
        model->enabled = enabled;
        output->event = enabled ? STEP_DIRECTION_EVENT_ENABLED :
                                  STEP_DIRECTION_EVENT_DISABLED;
        output->target_position_revolutions =
            model->target_position_revolutions;
        return true;
    }

    if (!enabled)
    {
        model->last_cumulative_steps = cumulative_steps;
        model->last_timestamp_us = timestamp_us;
        model->target_position_revolutions =
            current_position_revolutions;
        output->target_position_revolutions =
            model->target_position_revolutions;
        return true;
    }


    elapsed_us = timestamp_us - model->last_timestamp_us;
    if ((elapsed_us == 0u) ||
        (elapsed_us > model->config.maximum_sample_interval_us) ||
        !cumulative_delta(cumulative_steps,
                          model->last_cumulative_steps,
                          &delta_steps))
    {
        return false;
    }

    allowed_steps =
        (model->config.maximum_step_rate_per_second *
         ((float)elapsed_us * 1.0e-6f)) + 1.0f;
    if (fabsf((float)delta_steps) > allowed_steps)
    {
        return false;
    }

    model->target_position_revolutions +=
        (float)delta_steps / (float)model->config.steps_per_revolution;
    model->last_cumulative_steps = cumulative_steps;
    model->last_timestamp_us = timestamp_us;
    output->event = (delta_steps == 0) ? STEP_DIRECTION_EVENT_NONE :
                                         STEP_DIRECTION_EVENT_TARGET_UPDATED;
    output->target_position_revolutions =
        model->target_position_revolutions;
    output->delta_steps = delta_steps;
    return isfinite(model->target_position_revolutions);
}
