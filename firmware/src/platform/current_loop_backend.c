#include "mks57d/current_loop_backend.h"

#include <stddef.h>
#include <string.h>

#include "mks57d/adc1.h"
#include "mks57d/board.h"
#include "mks57d/interrupt_priority.h"
#include "mks57d/phase_current_reference.h"
#include "mks57d/tim3_bridge_pwm.h"
#include "mks57d/timebase.h"
#include "n32l40x.h"

enum
{
    NVIC_PRIORITY_SHIFT = 8u - __NVIC_PRIO_BITS,
    MAX_CONSECUTIVE_EMPTY_PWM_UPDATES = 1u,
    QUARTER_CYCLE_PHASE_Q32 = 0x40000000u
};

static phase_current_loop_config_t s_config;
static phase_current_loop_t s_loop;
static electrical_phase_predictor_t s_phase_predictor;
static volatile uint32_t s_fault_flags;
static volatile uint32_t s_sample_count;
static volatile uint32_t s_output_generation;
static int16_t s_last_reference_a_counts;
static int16_t s_last_reference_b_counts;
static int16_t s_aligned_q_reference_counts;
static uint32_t s_predicted_electrical_phase_q32;
static uint32_t s_phase_prediction_age_us;
static uint32_t s_guardian_generation;
static uint32_t s_guardian_empty_updates;
static volatile bool s_initialized;
static volatile bool s_active;
static bool s_phase_prediction_active;
static bool s_guardian_primed;
static phase_current_loop_output_t s_latest_output;
static current_loop_backend_trace_sample_t
    s_trace[CURRENT_LOOP_BACKEND_TRACE_CAPACITY];
static volatile uint16_t s_trace_count;

static uint32_t control_critical_enter(void)
{
    const uint32_t previous = __get_BASEPRI();
    const uint32_t threshold =
        (uint32_t)INTERRUPT_PRIORITY_CONTROL_GUARDIAN <<
        NVIC_PRIORITY_SHIFT;

    __set_BASEPRI_MAX(threshold);
    __DSB();
    __ISB();
    return previous;
}

static void control_critical_exit(uint32_t previous)
{
    __set_BASEPRI(previous);
    __DSB();
    __ISB();
}

static void fault_from_interrupt(uint32_t fault)
{
    s_active = false;
    s_fault_flags |= fault;
    phase_current_loop_stop(&s_loop);
    board_bridge_force_low_zero();
}

static bool predict_aligned_reference(
    const electrical_phase_predictor_t* predictor,
    int16_t q_current_reference_counts,
    uint32_t now_us,
    uint32_t* predicted_electrical_phase_q32,
    uint32_t* prediction_age_us,
    int16_t* current_a_reference_counts,
    int16_t* current_b_reference_counts)
{
    uint32_t phase_q32;

    if (!electrical_phase_predictor_predict(
            predictor,
            now_us,
            &phase_q32,
            prediction_age_us) ||
        !phase_current_reference_from_polar(
            q_current_reference_counts,
            phase_q32 + QUARTER_CYCLE_PHASE_Q32,
            current_a_reference_counts,
            current_b_reference_counts))
    {
        return false;
    }

    *predicted_electrical_phase_q32 = phase_q32;
    return true;
}

static void adc_current_event(adc1_status_t status,
                              const adc1_current_snapshot_t* snapshot,
                              void* context)
{
    phase_current_loop_output_t output;
    int16_t current_a_reference_counts;
    int16_t current_b_reference_counts;
    uint32_t predicted_phase_q32;
    uint32_t prediction_age_us;

    (void)context;
    if (status != ADC1_STATUS_OK)
    {
        fault_from_interrupt(CURRENT_LOOP_BACKEND_FAULT_ADC);
        return;
    }
    if (!s_active)
    {
        return;
    }
    if (snapshot == NULL)
    {
        fault_from_interrupt(CURRENT_LOOP_BACKEND_FAULT_INTERNAL);
        return;
    }
    if (s_phase_prediction_active)
    {
        if (!predict_aligned_reference(
                &s_phase_predictor,
                s_aligned_q_reference_counts,
                timebase_micros(),
                &predicted_phase_q32,
                &prediction_age_us,
                &current_a_reference_counts,
                &current_b_reference_counts))
        {
            fault_from_interrupt(
                CURRENT_LOOP_BACKEND_FAULT_PHASE_PREDICTION);
            return;
        }
        if (!phase_current_loop_set_reference_counts(
                &s_loop,
                &s_config,
                current_a_reference_counts,
                current_b_reference_counts))
        {
            const uint32_t phase_fault =
                s_loop.fault_flags & CURRENT_LOOP_BACKEND_FAULT_PHASE_MASK;

            fault_from_interrupt(
                phase_fault != 0u ? phase_fault :
                                    CURRENT_LOOP_BACKEND_FAULT_INTERNAL);
            return;
        }
        s_last_reference_a_counts = current_a_reference_counts;
        s_last_reference_b_counts = current_b_reference_counts;
        s_predicted_electrical_phase_q32 = predicted_phase_q32;
        s_phase_prediction_age_us = prediction_age_us;
    }
    if (!phase_current_loop_step(&s_loop,
                                 &s_config,
                                 snapshot->current_a_raw,
                                 snapshot->current_b_raw,
                                 &output))
    {
        const uint32_t phase_fault =
            s_loop.fault_flags & CURRENT_LOOP_BACKEND_FAULT_PHASE_MASK;

        s_latest_output = output;
        fault_from_interrupt(
            phase_fault != 0u ? phase_fault :
                                CURRENT_LOOP_BACKEND_FAULT_INTERNAL);
        return;
    }
    if (!s_active)
    {
        return;
    }
    if (!tim3_bridge_pwm_stage_duties(output.duty_permille))
    {
        fault_from_interrupt(CURRENT_LOOP_BACKEND_FAULT_PWM);
        return;
    }

    s_latest_output = output;
    if (s_trace_count < CURRENT_LOOP_BACKEND_TRACE_CAPACITY)
    {
        const uint16_t trace_index = s_trace_count;

        s_trace[trace_index].loop_sample_count = s_sample_count + 1u;
        s_trace[trace_index].current_a_reference_counts =
            s_last_reference_a_counts;
        s_trace[trace_index].current_b_reference_counts =
            s_last_reference_b_counts;
        s_trace[trace_index].current_a_measured_counts =
            output.current_a_measured_counts;
        s_trace[trace_index].current_b_measured_counts =
            output.current_b_measured_counts;
        s_trace[trace_index].phase_a_voltage_permille =
            output.phase_a_voltage_permille;
        s_trace[trace_index].phase_b_voltage_permille =
            output.phase_b_voltage_permille;
        __DMB();
        s_trace_count = trace_index + 1u;
    }
    __DMB();
    ++s_sample_count;
    ++s_output_generation;
}

static void pwm_update_event(void* context)
{
    const uint32_t generation = s_output_generation;

    (void)context;
    if (!s_active)
    {
        return;
    }
    if (!s_guardian_primed)
    {
        s_guardian_generation = generation;
        s_guardian_primed = true;
        return;
    }
    if (generation == s_guardian_generation)
    {
        ++s_guardian_empty_updates;
        if (s_guardian_empty_updates >
            MAX_CONSECUTIVE_EMPTY_PWM_UPDATES)
        {
            fault_from_interrupt(CURRENT_LOOP_BACKEND_FAULT_DEADLINE);
        }
        return;
    }
    s_guardian_generation = generation;
    s_guardian_empty_updates = 0u;
}

bool current_loop_backend_init(
    const phase_current_loop_config_t* config,
    const electrical_phase_predictor_config_t* phase_predictor_config)
{
    if (s_initialized || !phase_current_loop_config_is_valid(config) ||
        !electrical_phase_predictor_config_is_valid(
            phase_predictor_config))
    {
        return false;
    }

    memset(&s_latest_output, 0, sizeof(s_latest_output));
    s_config = *config;
    s_fault_flags = CURRENT_LOOP_BACKEND_FAULT_NONE;
    s_sample_count = 0u;
    s_output_generation = 0u;
    s_last_reference_a_counts = 0;
    s_last_reference_b_counts = 0;
    s_aligned_q_reference_counts = 0;
    s_predicted_electrical_phase_q32 = 0u;
    s_phase_prediction_age_us = 0u;
    s_guardian_generation = 0u;
    s_guardian_empty_updates = 0u;
    s_guardian_primed = false;
    s_trace_count = 0u;
    s_active = false;
    s_phase_prediction_active = false;
    s_initialized = false;

    if (!phase_current_loop_init(&s_loop, &s_config) ||
        !electrical_phase_predictor_init(
            &s_phase_predictor, phase_predictor_config) ||
        !adc1_set_current_event_handler(adc_current_event, NULL) ||
        !tim3_bridge_pwm_set_update_handler(pwm_update_event, NULL))
    {
        return false;
    }

    s_initialized = true;
    return true;
}

bool current_loop_backend_set_reference_counts(
    int16_t current_a_reference_counts,
    int16_t current_b_reference_counts)
{
    uint32_t previous;
    bool accepted;

    if (!s_initialized ||
        (s_fault_flags != CURRENT_LOOP_BACKEND_FAULT_NONE))
    {
        return false;
    }

    previous = control_critical_enter();
    accepted = phase_current_loop_set_reference_counts(
        &s_loop,
        &s_config,
        current_a_reference_counts,
        current_b_reference_counts);
    if (accepted)
    {
        electrical_phase_predictor_reset(&s_phase_predictor);
        s_phase_prediction_active = false;
        s_aligned_q_reference_counts = 0;
        s_predicted_electrical_phase_q32 = 0u;
        s_phase_prediction_age_us = 0u;
        s_last_reference_a_counts = current_a_reference_counts;
        s_last_reference_b_counts = current_b_reference_counts;
    }
    if (!accepted)
    {
        const uint32_t phase_fault =
            s_loop.fault_flags & CURRENT_LOOP_BACKEND_FAULT_PHASE_MASK;

        s_active = false;
        s_fault_flags |= phase_fault != 0u ? phase_fault :
                                             CURRENT_LOOP_BACKEND_FAULT_INTERNAL;
    }
    control_critical_exit(previous);

    if (!accepted)
    {
        board_bridge_force_low_zero();
    }
    return accepted;
}

bool current_loop_backend_set_aligned_q_reference(
    int16_t q_current_reference_counts,
    uint32_t electrical_phase_q32,
    int32_t mechanical_velocity_revolutions_per_second_q16_16,
    int8_t encoder_direction,
    uint32_t encoder_timestamp_us)
{
    electrical_phase_predictor_t candidate;
    int16_t current_a_reference_counts = 0;
    int16_t current_b_reference_counts = 0;
    uint32_t predicted_phase_q32 = 0u;
    uint32_t prediction_age_us = 0u;
    uint32_t previous;
    const uint32_t now_us = timebase_micros();
    int32_t q_magnitude = q_current_reference_counts;
    bool candidate_valid;
    bool accepted = false;

    if (!s_initialized ||
        (s_fault_flags != CURRENT_LOOP_BACKEND_FAULT_NONE))
    {
        return false;
    }
    if (q_magnitude < 0)
    {
        q_magnitude = -q_magnitude;
    }

    candidate = s_phase_predictor;
    candidate_valid =
        ((uint32_t)q_magnitude <= s_config.reference_limit_counts) &&
        electrical_phase_predictor_set_observation(
            &candidate,
            electrical_phase_q32,
            mechanical_velocity_revolutions_per_second_q16_16,
            encoder_direction,
            encoder_timestamp_us) &&
        predict_aligned_reference(
            &candidate,
            q_current_reference_counts,
            now_us,
            &predicted_phase_q32,
            &prediction_age_us,
            &current_a_reference_counts,
            &current_b_reference_counts);

    previous = control_critical_enter();
    if (s_fault_flags == CURRENT_LOOP_BACKEND_FAULT_NONE)
    {
        if (candidate_valid && phase_current_loop_set_reference_counts(
                &s_loop,
                &s_config,
                current_a_reference_counts,
                current_b_reference_counts))
        {
            s_phase_predictor = candidate;
            s_phase_prediction_active = true;
            s_aligned_q_reference_counts = q_current_reference_counts;
            s_predicted_electrical_phase_q32 = predicted_phase_q32;
            s_phase_prediction_age_us = prediction_age_us;
            s_last_reference_a_counts = current_a_reference_counts;
            s_last_reference_b_counts = current_b_reference_counts;
            accepted = true;
        }
        else
        {
            const uint32_t phase_fault =
                s_loop.fault_flags & CURRENT_LOOP_BACKEND_FAULT_PHASE_MASK;

            s_active = false;
            s_fault_flags |= candidate_valid ?
                (phase_fault != 0u ? phase_fault :
                                     CURRENT_LOOP_BACKEND_FAULT_INTERNAL) :
                CURRENT_LOOP_BACKEND_FAULT_PHASE_PREDICTION;
        }
    }
    control_critical_exit(previous);

    if (!accepted)
    {
        board_bridge_force_low_zero();
    }
    return accepted;
}

bool current_loop_backend_start(void)
{
    static const uint16_t zero_duties[
        TIM3_BRIDGE_PWM_CHANNEL_COUNT] = {0u, 0u, 0u, 0u};
    uint32_t previous;

    if (!s_initialized || s_active ||
        (s_fault_flags != CURRENT_LOOP_BACKEND_FAULT_NONE) ||
        !tim3_bridge_pwm_stage_duties(zero_duties))
    {
        return false;
    }

    previous = control_critical_enter();
    s_output_generation = 0u;
    s_guardian_generation = 0u;
    s_guardian_empty_updates = 0u;
    s_guardian_primed = false;
    s_trace_count = 0u;
    if (!phase_current_loop_start(&s_loop))
    {
        s_fault_flags |= CURRENT_LOOP_BACKEND_FAULT_INTERNAL;
        control_critical_exit(previous);
        board_bridge_force_low_zero();
        return false;
    }
    s_active = true;
    control_critical_exit(previous);

    if (!tim3_bridge_pwm_update_irq_enable(true))
    {
        fault_from_interrupt(CURRENT_LOOP_BACKEND_FAULT_PWM);
        return false;
    }
    return true;
}

bool current_loop_backend_stop(void)
{
    uint32_t previous;
    bool fault_was_latched;

    if (!s_initialized)
    {
        return false;
    }

    previous = control_critical_enter();
    fault_was_latched =
        s_fault_flags != CURRENT_LOOP_BACKEND_FAULT_NONE;
    s_active = false;
    (void)tim3_bridge_pwm_update_irq_enable(false);
    phase_current_loop_stop(&s_loop);
    electrical_phase_predictor_reset(&s_phase_predictor);
    s_phase_prediction_active = false;
    s_aligned_q_reference_counts = 0;
    s_predicted_electrical_phase_q32 = 0u;
    s_phase_prediction_age_us = 0u;
    s_last_reference_a_counts = 0;
    s_last_reference_b_counts = 0;
    control_critical_exit(previous);

    /* The interrupt fault path has already forced the direct-GPIO ZERO vector
       and stopped TIM3. Treat that established safe state as a successful,
       idempotent stop so foreground can preserve the initiating fault. */
    if (fault_was_latched)
    {
        board_bridge_force_low_zero();
        return true;
    }

    if (!tim3_bridge_pwm_zero())
    {
        board_bridge_force_low_zero();
        s_fault_flags |= CURRENT_LOOP_BACKEND_FAULT_PWM;
        return false;
    }
    return true;
}

void current_loop_backend_get_snapshot(
    current_loop_backend_snapshot_t* snapshot)
{
    uint32_t previous;

    if (snapshot == NULL)
    {
        return;
    }

    previous = control_critical_enter();
    snapshot->latest_output = s_latest_output;
    snapshot->current_a_reference_counts = s_last_reference_a_counts;
    snapshot->current_b_reference_counts = s_last_reference_b_counts;
    snapshot->sample_count = s_sample_count;
    snapshot->fault_flags = s_fault_flags;
    snapshot->predicted_electrical_phase_q32 =
        s_predicted_electrical_phase_q32;
    snapshot->electrical_phase_rate_q32_per_us =
        s_phase_predictor.electrical_phase_rate_q32_per_us;
    snapshot->phase_prediction_age_us = s_phase_prediction_age_us;
    snapshot->maximum_phase_prediction_age_us =
        s_phase_predictor.config.maximum_prediction_age_us;
    snapshot->phase_prediction_output_lead_us =
        s_phase_predictor.config.output_lead_us;
    snapshot->initialized = s_initialized;
    snapshot->active = s_active;
    snapshot->phase_prediction_active = s_phase_prediction_active;
    control_critical_exit(previous);
}

uint16_t current_loop_backend_trace_count(void)
{
    uint32_t previous = control_critical_enter();
    const uint16_t count = s_trace_count;

    control_critical_exit(previous);
    return count;
}

bool current_loop_backend_trace_get(
    uint16_t index,
    current_loop_backend_trace_sample_t* sample)
{
    uint32_t previous;

    if ((sample == NULL) || !s_initialized)
    {
        return false;
    }

    previous = control_critical_enter();
    if (s_active || (index >= s_trace_count))
    {
        control_critical_exit(previous);
        return false;
    }
    *sample = s_trace[index];
    control_critical_exit(previous);
    return true;
}
