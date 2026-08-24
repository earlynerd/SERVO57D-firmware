#include "mks57d/phase_current_loop.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#include "mks57d/adc_limits.h"
#include "mks57d/phase_current_reference.h"

enum
{
    PHASE_CURRENT_LOOP_Q15_SHIFT = 15u,
    PHASE_CURRENT_LOOP_Q15_HALF = 1u << 14u
};

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

bool phase_current_loop_set_reference_counts_prevalidated(
    phase_current_loop_t* loop,
    const phase_current_loop_config_t* config,
    int16_t current_a_reference_counts,
    int16_t current_b_reference_counts)
{
    if ((loop == NULL) || (config == NULL) || !loop->initialized ||
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

bool phase_current_loop_set_reference_counts(
    phase_current_loop_t* loop,
    const phase_current_loop_config_t* config,
    int16_t current_a_reference_counts,
    int16_t current_b_reference_counts)
{
    if (!phase_current_loop_config_is_valid(config))
    {
        return false;
    }
    return phase_current_loop_set_reference_counts_prevalidated(
        loop,
        config,
        current_a_reference_counts,
        current_b_reference_counts);
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

static bool measure_phase_currents(
    phase_current_loop_t* loop,
    const phase_current_loop_config_t* config,
    uint16_t current_a_raw,
    uint16_t current_b_raw,
    phase_current_loop_output_t* output)
{
    int32_t current_a_delta;
    int32_t current_b_delta;

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
    return loop->fault_flags == PHASE_CURRENT_LOOP_FAULT_NONE;
}

static bool publish_phase_voltage(
    phase_current_loop_t* loop,
    const phase_current_loop_config_t* config,
    int16_t phase_a_voltage_permille,
    int16_t phase_b_voltage_permille,
    phase_current_loop_output_t* output)
{
    output->phase_a_voltage_permille = phase_a_voltage_permille;
    output->phase_b_voltage_permille = phase_b_voltage_permille;
    if (!build_duties(phase_a_voltage_permille,
                      phase_b_voltage_permille,
                      config,
                      output))
    {
        latch_fault(loop, PHASE_CURRENT_LOOP_FAULT_INVALID_OUTPUT);
        zero_output(output);
        return false;
    }
    return true;
}

bool phase_current_loop_step_prevalidated(
    phase_current_loop_t* loop,
    const phase_current_loop_config_t* config,
    uint16_t current_a_raw,
    uint16_t current_b_raw,
    phase_current_loop_output_t* output)
{
    int32_t output_limit_q16;
    int16_t phase_a_voltage_permille;
    int16_t phase_b_voltage_permille;

    zero_output(output);
    if ((loop == NULL) || (config == NULL) || (output == NULL) ||
        !loop->initialized || !loop->running ||
        (loop->fault_flags != PHASE_CURRENT_LOOP_FAULT_NONE))
    {
        return false;
    }
    if (!measure_phase_currents(loop,
                                config,
                                current_a_raw,
                                current_b_raw,
                                output))
    {
        return false;
    }

    output_limit_q16 =
        (int32_t)config->phase_voltage_limit_permille <<
        PHASE_CURRENT_LOOP_Q16_SHIFT;
    phase_a_voltage_permille = controller_axis_step(
        loop->current_a_reference_counts,
        output->current_a_measured_counts,
        config->proportional_gain_q16_per_count,
        config->integral_gain_q16_per_count_per_step,
        output_limit_q16,
        &loop->current_a_integrator_q16);
    phase_b_voltage_permille = controller_axis_step(
        loop->current_b_reference_counts,
        output->current_b_measured_counts,
        config->proportional_gain_q16_per_count,
        config->integral_gain_q16_per_count_per_step,
        output_limit_q16,
        &loop->current_b_integrator_q16);

    return publish_phase_voltage(loop,
                                 config,
                                 phase_a_voltage_permille,
                                 phase_b_voltage_permille,
                                 output);
}

static int64_t round_shift_q15(int64_t value)
{
    uint64_t magnitude = value < 0 ?
        (uint64_t)(-value) : (uint64_t)value;

    magnitude = (magnitude + PHASE_CURRENT_LOOP_Q15_HALF) >>
                PHASE_CURRENT_LOOP_Q15_SHIFT;
    return value < 0 ? -(int64_t)magnitude : (int64_t)magnitude;
}

static void park_current_counts(int16_t current_a_counts,
                                int16_t current_b_counts,
                                int16_t sine_q15,
                                int16_t cosine_q15,
                                int32_t* current_d_counts,
                                int32_t* current_q_counts)
{
    *current_d_counts = (int32_t)round_shift_q15(
        (int64_t)current_a_counts * cosine_q15 +
        (int64_t)current_b_counts * sine_q15);
    *current_q_counts = (int32_t)round_shift_q15(
        -(int64_t)current_a_counts * sine_q15 +
        (int64_t)current_b_counts * cosine_q15);
}

static void inverse_park_voltage_q16(int64_t voltage_d_q16,
                                     int64_t voltage_q_q16,
                                     int16_t sine_q15,
                                     int16_t cosine_q15,
                                     int64_t* voltage_a_q16,
                                     int64_t* voltage_b_q16)
{
    *voltage_a_q16 = round_shift_q15(
        voltage_d_q16 * cosine_q15 - voltage_q_q16 * sine_q15);
    *voltage_b_q16 = round_shift_q15(
        voltage_d_q16 * sine_q15 + voltage_q_q16 * cosine_q15);
}

static int32_t controller_axis_candidate_integrator(
    int32_t error_counts,
    int32_t integral_gain_q16,
    int32_t output_limit_q16,
    int32_t previous_integrator_q16)
{
    return clamp_i64_to_i32(
        (int64_t)previous_integrator_q16 +
            (int64_t)error_counts * integral_gain_q16,
        -output_limit_q16,
        output_limit_q16);
}

static int64_t absolute_i64(int64_t value)
{
    return value < 0 ? -value : value;
}

static int64_t phase_voltage_peak_q16(int64_t voltage_a_q16,
                                      int64_t voltage_b_q16)
{
    const int64_t magnitude_a = absolute_i64(voltage_a_q16);
    const int64_t magnitude_b = absolute_i64(voltage_b_q16);

    return magnitude_a > magnitude_b ? magnitude_a : magnitude_b;
}

static void calculate_rotating_phase_voltage_q16(
    int32_t error_d_counts,
    int32_t error_q_counts,
    int32_t proportional_gain_q16,
    int32_t integrator_d_q16,
    int32_t integrator_q_q16,
    int16_t application_sine_q15,
    int16_t application_cosine_q15,
    int64_t* voltage_a_q16,
    int64_t* voltage_b_q16)
{
    const int64_t voltage_d_q16 =
        (int64_t)error_d_counts * proportional_gain_q16 +
        integrator_d_q16;
    const int64_t voltage_q_q16 =
        (int64_t)error_q_counts * proportional_gain_q16 +
        integrator_q_q16;

    inverse_park_voltage_q16(voltage_d_q16,
                             voltage_q_q16,
                             application_sine_q15,
                             application_cosine_q15,
                             voltage_a_q16,
                             voltage_b_q16);
}

static uint16_t phase_voltage_scale_q15(int64_t peak_q16,
                                        int32_t limit_q16)
{
    uint32_t scale_q15 = 0u;
    uint32_t bit;
    const uint64_t scaled_limit =
        (uint64_t)(uint32_t)limit_q16 << PHASE_CURRENT_LOOP_Q15_SHIFT;

    /* Fixed-iteration restoring search for floor(limit / peak) in Q1.15.
       Saturation calls this only for 0 < limit < peak, so 0x7FFF is the
       largest relevant result. This avoids a variable divide in the ISR. */
    for (bit = 1u << 14u; bit != 0u; bit >>= 1u)
    {
        const uint32_t candidate = scale_q15 | bit;

        if ((uint64_t)peak_q16 * candidate <= scaled_limit)
        {
            scale_q15 = candidate;
        }
    }
    return (uint16_t)scale_q15;
}

static int64_t scale_signed_q15(int64_t value, uint16_t scale_q15)
{
    uint64_t magnitude = value < 0 ?
        (uint64_t)(-value) : (uint64_t)value;

    magnitude = (magnitude * scale_q15) >>
                PHASE_CURRENT_LOOP_Q15_SHIFT;
    return value < 0 ? -(int64_t)magnitude : (int64_t)magnitude;
}

static int16_t round_q16_to_i16(int64_t value_q16)
{
    uint64_t magnitude = value_q16 < 0 ?
        (uint64_t)(-value_q16) : (uint64_t)value_q16;

    magnitude = (magnitude + (PHASE_CURRENT_LOOP_Q16_ONE / 2u)) >>
                PHASE_CURRENT_LOOP_Q16_SHIFT;
    return value_q16 < 0 ?
        (int16_t)(-(int32_t)magnitude) : (int16_t)magnitude;
}

bool phase_current_loop_step_rotating_prevalidated(
    phase_current_loop_t* loop,
    const phase_current_loop_config_t* config,
    uint16_t current_a_raw,
    uint16_t current_b_raw,
    int16_t current_d_reference_counts,
    int16_t current_q_reference_counts,
    uint32_t sample_electrical_phase_q32,
    uint32_t pwm_application_phase_q32,
    phase_current_loop_output_t* output)
{
    int16_t sample_sine_q15;
    int16_t sample_cosine_q15;
    int16_t application_sine_q15;
    int16_t application_cosine_q15;
    int32_t current_d_measured_counts;
    int32_t current_q_measured_counts;
    int32_t error_d_counts;
    int32_t error_q_counts;
    int32_t candidate_integrator_d_q16;
    int32_t candidate_integrator_q_q16;
    int32_t output_limit_q16;
    int64_t voltage_a_q16;
    int64_t voltage_b_q16;
    int64_t voltage_peak_q16;

    zero_output(output);
    if ((loop == NULL) || (config == NULL) || (output == NULL) ||
        !loop->initialized || !loop->running ||
        (loop->fault_flags != PHASE_CURRENT_LOOP_FAULT_NONE))
    {
        return false;
    }
    if (!measure_phase_currents(loop,
                                config,
                                current_a_raw,
                                current_b_raw,
                                output))
    {
        return false;
    }
    if ((absolute_i32(current_d_reference_counts) >
         config->reference_limit_counts) ||
        (absolute_i32(current_q_reference_counts) >
         config->reference_limit_counts))
    {
        latch_fault(loop, PHASE_CURRENT_LOOP_FAULT_INVALID_REFERENCE);
        return false;
    }

    loop->current_a_reference_counts = current_d_reference_counts;
    loop->current_b_reference_counts = current_q_reference_counts;
    (void)phase_current_reference_sin_cos_q15(
        sample_electrical_phase_q32,
        &sample_sine_q15,
        &sample_cosine_q15);
    (void)phase_current_reference_sin_cos_q15(
        pwm_application_phase_q32,
        &application_sine_q15,
        &application_cosine_q15);
    park_current_counts(output->current_a_measured_counts,
                        output->current_b_measured_counts,
                        sample_sine_q15,
                        sample_cosine_q15,
                        &current_d_measured_counts,
                        &current_q_measured_counts);
    error_d_counts = (int32_t)current_d_reference_counts -
                     current_d_measured_counts;
    error_q_counts = (int32_t)current_q_reference_counts -
                     current_q_measured_counts;
    output_limit_q16 =
        (int32_t)config->phase_voltage_limit_permille <<
        PHASE_CURRENT_LOOP_Q16_SHIFT;
    candidate_integrator_d_q16 = controller_axis_candidate_integrator(
        error_d_counts,
        config->integral_gain_q16_per_count_per_step,
        output_limit_q16,
        loop->current_a_integrator_q16);
    candidate_integrator_q_q16 = controller_axis_candidate_integrator(
        error_q_counts,
        config->integral_gain_q16_per_count_per_step,
        output_limit_q16,
        loop->current_b_integrator_q16);
    calculate_rotating_phase_voltage_q16(
        error_d_counts,
        error_q_counts,
        config->proportional_gain_q16_per_count,
        candidate_integrator_d_q16,
        candidate_integrator_q_q16,
        application_sine_q15,
        application_cosine_q15,
        &voltage_a_q16,
        &voltage_b_q16);
    voltage_peak_q16 = phase_voltage_peak_q16(
        voltage_a_q16, voltage_b_q16);

    if (voltage_peak_q16 > output_limit_q16)
    {
        int64_t previous_voltage_a_q16;
        int64_t previous_voltage_b_q16;
        int64_t previous_peak_q16;

        calculate_rotating_phase_voltage_q16(
            error_d_counts,
            error_q_counts,
            config->proportional_gain_q16_per_count,
            loop->current_a_integrator_q16,
            loop->current_b_integrator_q16,
            application_sine_q15,
            application_cosine_q15,
            &previous_voltage_a_q16,
            &previous_voltage_b_q16);
        previous_peak_q16 = phase_voltage_peak_q16(
            previous_voltage_a_q16, previous_voltage_b_q16);
        if (voltage_peak_q16 >= previous_peak_q16)
        {
            voltage_a_q16 = previous_voltage_a_q16;
            voltage_b_q16 = previous_voltage_b_q16;
            voltage_peak_q16 = previous_peak_q16;
        }
        else
        {
            loop->current_a_integrator_q16 =
                candidate_integrator_d_q16;
            loop->current_b_integrator_q16 =
                candidate_integrator_q_q16;
        }
    }
    else
    {
        loop->current_a_integrator_q16 = candidate_integrator_d_q16;
        loop->current_b_integrator_q16 = candidate_integrator_q_q16;
    }

    if (voltage_peak_q16 > output_limit_q16)
    {
        const uint16_t scale_q15 = phase_voltage_scale_q15(
            voltage_peak_q16, output_limit_q16);

        voltage_a_q16 = scale_signed_q15(voltage_a_q16, scale_q15);
        voltage_b_q16 = scale_signed_q15(voltage_b_q16, scale_q15);
    }
    return publish_phase_voltage(
        loop,
        config,
        round_q16_to_i16(voltage_a_q16),
        round_q16_to_i16(voltage_b_q16),
        output);
}

bool phase_current_loop_step(phase_current_loop_t* loop,
                             const phase_current_loop_config_t* config,
                             uint16_t current_a_raw,
                             uint16_t current_b_raw,
                             phase_current_loop_output_t* output)
{
    zero_output(output);
    if (!phase_current_loop_config_is_valid(config))
    {
        return false;
    }
    return phase_current_loop_step_prevalidated(loop,
                                                config,
                                                current_a_raw,
                                                current_b_raw,
                                                output);
}
