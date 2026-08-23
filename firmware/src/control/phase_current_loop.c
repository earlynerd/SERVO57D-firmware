#include "mks57d/phase_current_loop.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#include "mks57d/adc_limits.h"

static int32_t absolute_i32(int32_t value)
{
    return value < 0 ? -value : value;
}

static int32_t clamp_i64_to_i32(int64_t value,
                                int32_t minimum,
                                int32_t maximum)
{
    if (value < (int64_t)minimum)
    {
        return minimum;
    }
    if (value > (int64_t)maximum)
    {
        return maximum;
    }
    return (int32_t)value;
}

static void zero_output(phase_current_loop_output_t* output)
{
    if (output != NULL)
    {
        memset(output, 0, sizeof(*output));
    }
}

static bool polarity_is_valid(int8_t polarity)
{
    return (polarity == 1) || (polarity == -1);
}

bool phase_current_loop_config_is_valid(
    const phase_current_loop_config_t* config)
{
    uint32_t maximum_phase_voltage;

    if (config == NULL)
    {
        return false;
    }

    maximum_phase_voltage =
        PHASE_CURRENT_LOOP_DUTY_FULL_SCALE_PERMILLE -
        (uint32_t)config->duty_margin_permille;

    return (config->current_a_zero_raw <= ADC_SAMPLE_RAW_MAX) &&
           (config->current_b_zero_raw <= ADC_SAMPLE_RAW_MAX) &&
           (config->hard_current_limit_counts != 0u) &&
           (config->reference_limit_counts != 0u) &&
           (config->reference_limit_counts <
            config->hard_current_limit_counts) &&
           (config->current_a_zero_raw >=
            config->hard_current_limit_counts) &&
           (((uint32_t)config->current_a_zero_raw +
             config->hard_current_limit_counts) <= ADC_SAMPLE_RAW_MAX) &&
           (config->current_b_zero_raw >=
            config->hard_current_limit_counts) &&
           (((uint32_t)config->current_b_zero_raw +
             config->hard_current_limit_counts) <= ADC_SAMPLE_RAW_MAX) &&
           (config->proportional_gain_q16_per_count >= 0) &&
           (config->proportional_gain_q16_per_count <=
            PHASE_CURRENT_LOOP_PROPORTIONAL_GAIN_MAXIMUM_Q16) &&
           (config->integral_gain_q16_per_count_per_step >= 0) &&
           (config->integral_gain_q16_per_count_per_step <=
            PHASE_CURRENT_LOOP_INTEGRAL_GAIN_MAXIMUM_Q16) &&
           (config->phase_voltage_limit_permille != 0u) &&
           (config->duty_margin_permille <
            PHASE_CURRENT_LOOP_DUTY_FULL_SCALE_PERMILLE) &&
           ((uint32_t)config->phase_voltage_limit_permille <=
            maximum_phase_voltage) &&
           polarity_is_valid(config->current_a_polarity) &&
           polarity_is_valid(config->current_b_polarity);
}

bool phase_current_loop_init(phase_current_loop_t* loop,
                             const phase_current_loop_config_t* config)
{
    if ((loop == NULL) || !phase_current_loop_config_is_valid(config))
    {
        return false;
    }

    memset(loop, 0, sizeof(*loop));
    loop->initialized = true;
    return true;
}

static void latch_fault(phase_current_loop_t* loop, uint32_t fault)
{
    loop->fault_flags |= fault;
    loop->running = false;
    loop->current_a_integrator_q16 = 0;
    loop->current_b_integrator_q16 = 0;
}

bool phase_current_loop_set_reference_counts(
    phase_current_loop_t* loop,
    const phase_current_loop_config_t* config,
    int16_t current_a_reference_counts,
    int16_t current_b_reference_counts)
{
    if ((loop == NULL) || !loop->initialized ||
        !phase_current_loop_config_is_valid(config) ||
        (loop->fault_flags != PHASE_CURRENT_LOOP_FAULT_NONE))
    {
        return false;
    }
    if ((absolute_i32(current_a_reference_counts) >
         config->reference_limit_counts) ||
        (absolute_i32(current_b_reference_counts) >
         config->reference_limit_counts))
    {
        latch_fault(loop, PHASE_CURRENT_LOOP_FAULT_INVALID_REFERENCE);
        return false;
    }

    loop->current_a_reference_counts = current_a_reference_counts;
    loop->current_b_reference_counts = current_b_reference_counts;
    return true;
}

bool phase_current_loop_start(phase_current_loop_t* loop)
{
    if ((loop == NULL) || !loop->initialized ||
        (loop->fault_flags != PHASE_CURRENT_LOOP_FAULT_NONE))
    {
        return false;
    }

    loop->current_a_integrator_q16 = 0;
    loop->current_b_integrator_q16 = 0;
    loop->running = true;
    return true;
}

void phase_current_loop_stop(phase_current_loop_t* loop)
{
    if (loop == NULL)
    {
        return;
    }

    loop->running = false;
    loop->current_a_integrator_q16 = 0;
    loop->current_b_integrator_q16 = 0;
    loop->current_a_reference_counts = 0;
    loop->current_b_reference_counts = 0;
}

static int16_t controller_axis_step(int16_t reference,
                                    int16_t measured,
                                    int32_t proportional_gain_q16,
                                    int32_t integral_gain_q16,
                                    int32_t output_limit_q16,
                                    int32_t* integrator_q16)
{
    const int32_t error = (int32_t)reference - (int32_t)measured;
    const int64_t proportional =
        (int64_t)error * (int64_t)proportional_gain_q16;
    const int32_t previous_integrator = *integrator_q16;
    int32_t candidate_integrator = clamp_i64_to_i32(
        (int64_t)previous_integrator +
            ((int64_t)error * (int64_t)integral_gain_q16),
        -output_limit_q16,
        output_limit_q16);
    int64_t unconstrained = proportional + candidate_integrator;

    if (((unconstrained > output_limit_q16) && (error > 0)) ||
        ((unconstrained < -output_limit_q16) && (error < 0)))
    {
        candidate_integrator = previous_integrator;
        unconstrained = proportional + candidate_integrator;
    }

    *integrator_q16 = candidate_integrator;
    unconstrained = clamp_i64_to_i32(unconstrained,
                                     -output_limit_q16,
                                     output_limit_q16);
    if (unconstrained >= 0)
    {
        unconstrained += PHASE_CURRENT_LOOP_Q16_ONE / 2u;
    }
    else
    {
        unconstrained -= PHASE_CURRENT_LOOP_Q16_ONE / 2u;
    }
    return (int16_t)(unconstrained / PHASE_CURRENT_LOOP_Q16_ONE);
}

static bool build_duties(int16_t command_a,
                         int16_t command_b,
                         const phase_current_loop_config_t* config,
                         phase_current_loop_output_t* output)
{
    const int32_t duty_values[PHASE_CURRENT_LOOP_CHANNEL_COUNT] = {
        command_a < 0 ? -(int32_t)command_a : 0,
        command_a > 0 ? command_a : 0,
        command_b > 0 ? command_b : 0,
        command_b < 0 ? -(int32_t)command_b : 0,
    };
    const int32_t maximum_active_duty =
        (int32_t)(PHASE_CURRENT_LOOP_DUTY_FULL_SCALE_PERMILLE -
                  config->duty_margin_permille);
    uint32_t channel;

    for (channel = 0u;
         channel < PHASE_CURRENT_LOOP_CHANNEL_COUNT;
         ++channel)
    {
        if ((duty_values[channel] < 0) ||
            (duty_values[channel] > maximum_active_duty))
        {
            return false;
        }
        output->duty_permille[channel] =
            (uint16_t)duty_values[channel];
    }
    return true;
}

bool phase_current_loop_step(phase_current_loop_t* loop,
                             const phase_current_loop_config_t* config,
                             uint16_t current_a_raw,
                             uint16_t current_b_raw,
                             phase_current_loop_output_t* output)
{
    int32_t current_a_delta;
    int32_t current_b_delta;
    int32_t output_limit_q16;

    zero_output(output);
    if ((loop == NULL) || (output == NULL) || !loop->initialized ||
        !loop->running || !phase_current_loop_config_is_valid(config) ||
        (loop->fault_flags != PHASE_CURRENT_LOOP_FAULT_NONE))
    {
        return false;
    }
    if ((current_a_raw > ADC_SAMPLE_RAW_MAX) ||
        (current_b_raw > ADC_SAMPLE_RAW_MAX))
    {
        latch_fault(loop, PHASE_CURRENT_LOOP_FAULT_INVALID_SAMPLE);
        return false;
    }

    current_a_delta = (int32_t)current_a_raw -
                      (int32_t)config->current_a_zero_raw;
    current_b_delta = (int32_t)current_b_raw -
                      (int32_t)config->current_b_zero_raw;
    output->current_a_measured_counts =
        (int16_t)(current_a_delta * config->current_a_polarity);
    output->current_b_measured_counts =
        (int16_t)(current_b_delta * config->current_b_polarity);
    if (absolute_i32(current_a_delta) >
        config->hard_current_limit_counts)
    {
        latch_fault(loop, PHASE_CURRENT_LOOP_FAULT_OVERCURRENT_A);
    }
    if (absolute_i32(current_b_delta) >
        config->hard_current_limit_counts)
    {
        latch_fault(loop, PHASE_CURRENT_LOOP_FAULT_OVERCURRENT_B);
    }
    if (loop->fault_flags != PHASE_CURRENT_LOOP_FAULT_NONE)
    {
        return false;
    }

    output_limit_q16 =
        (int32_t)config->phase_voltage_limit_permille <<
        PHASE_CURRENT_LOOP_Q16_SHIFT;
    output->phase_a_voltage_permille = controller_axis_step(
        loop->current_a_reference_counts,
        output->current_a_measured_counts,
        config->proportional_gain_q16_per_count,
        config->integral_gain_q16_per_count_per_step,
        output_limit_q16,
        &loop->current_a_integrator_q16);
    output->phase_b_voltage_permille = controller_axis_step(
        loop->current_b_reference_counts,
        output->current_b_measured_counts,
        config->proportional_gain_q16_per_count,
        config->integral_gain_q16_per_count_per_step,
        output_limit_q16,
        &loop->current_b_integrator_q16);

    if (!build_duties(output->phase_a_voltage_permille,
                      output->phase_b_voltage_permille,
                      config,
                      output))
    {
        latch_fault(loop, PHASE_CURRENT_LOOP_FAULT_INVALID_OUTPUT);
        zero_output(output);
        return false;
    }
    return true;
}
