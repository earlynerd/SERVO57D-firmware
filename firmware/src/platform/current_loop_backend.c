#include "mks57d/current_loop_backend.h"

#include <stddef.h>
#include <string.h>

#include "mks57d/adc1.h"
#include "mks57d/board.h"
#include "mks57d/cycle_counter.h"
#include "mks57d/interrupt_priority.h"
#include "mks57d/phase_current_reference.h"
#include "mks57d/platform.h"
#include "mks57d/rotating_current_test.h"
#include "mks57d/tim3_bridge_pwm.h"
#include "mks57d/timebase.h"
#include "n32l40x.h"

enum
{
    NVIC_PRIORITY_SHIFT = 8u - __NVIC_PRIO_BITS,
    MAX_CONSECUTIVE_EMPTY_PWM_UPDATES = 1u,
    QUARTER_CYCLE_PHASE_Q32 = 0x40000000u
};

_Static_assert(sizeof(current_loop_backend_trace_sample_t) == 32u,
               "current trace sample must retain its SRAM budget");

static phase_current_loop_config_t s_config;
static electrical_phase_predictor_config_t s_phase_predictor_config;
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
static uint32_t s_maximum_observed_phase_prediction_age_us;
static uint32_t s_rejected_phase_prediction_age_us;
static uint32_t s_guardian_generation;
static uint32_t s_guardian_empty_updates;
static uint32_t s_guardian_missed_update_count;
static uint32_t s_guardian_maximum_empty_updates;
static uint8_t s_phase_prediction_reject_reason;
static volatile bool s_initialized;
static volatile bool s_active;
static bool s_phase_prediction_active;
static bool s_rotating_reference_active;
static bool s_guardian_primed;
static rotating_current_test_t s_rotating_reference;
static phase_current_loop_output_t s_latest_output;
static current_loop_backend_trace_sample_t
    s_trace[CURRENT_LOOP_BACKEND_TRACE_CAPACITY];
static volatile uint16_t s_trace_count;
static volatile bool s_trace_armed;

static uint16_t saturate_u16(uint32_t value)
{
    return value > UINT16_MAX ? UINT16_MAX : (uint16_t)value;
}

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
    s_rotating_reference_active = false;
    s_trace_armed = false;
    (void)adc1_set_current_timing_capture(false);
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
    int16_t* current_b_reference_counts,
    current_loop_phase_prediction_reject_t* rejection_reason)
{
    uint32_t phase_q32;

    if (rejection_reason != NULL)
    {
        *rejection_reason = CURRENT_LOOP_PHASE_PREDICTION_REJECT_NONE;
    }
    if (!electrical_phase_predictor_predict(
            predictor,
            now_us,
            &phase_q32,
            prediction_age_us))
    {
        if (rejection_reason != NULL)
        {
            *rejection_reason =
                ((predictor != NULL) && predictor->initialized &&
                 predictor->observation_valid) ?
                    CURRENT_LOOP_PHASE_PREDICTION_REJECT_STALE :
                    CURRENT_LOOP_PHASE_PREDICTION_REJECT_OBSERVATION_INVALID;
        }
        return false;
    }
    if (!phase_current_reference_from_polar(
            q_current_reference_counts,
            phase_q32 + QUARTER_CYCLE_PHASE_Q32,
            current_a_reference_counts,
            current_b_reference_counts))
    {
        if (rejection_reason != NULL)
        {
            *rejection_reason =
                CURRENT_LOOP_PHASE_PREDICTION_REJECT_REFERENCE_MAPPING;
        }
        return false;
    }

    *predicted_electrical_phase_q32 = phase_q32;
    return true;
}

static void record_prediction_age(uint32_t prediction_age_us)
{
    s_phase_prediction_age_us = prediction_age_us;
    if (prediction_age_us >
        s_maximum_observed_phase_prediction_age_us)
    {
        s_maximum_observed_phase_prediction_age_us = prediction_age_us;
    }
}

static void record_prediction_rejection(
    current_loop_phase_prediction_reject_t reason,
    uint32_t prediction_age_us)
{
    s_phase_prediction_reject_reason = (uint8_t)reason;
    s_rejected_phase_prediction_age_us = prediction_age_us;
}

static void adc_current_event(adc1_status_t status,
                              const adc1_current_snapshot_t* snapshot,
                              void* context)
{
    phase_current_loop_output_t output;
    int16_t current_a_reference_counts;
    int16_t current_b_reference_counts;
    uint32_t predicted_phase_q32;
    uint32_t prediction_age_us = 0u;
    current_loop_phase_prediction_reject_t rejection_reason =
        CURRENT_LOOP_PHASE_PREDICTION_REJECT_NONE;
    uint16_t pwm_preload_margin_ticks = 0u;
    uint32_t pwm_stage_cycle_count = 0u;
    bool trace_armed;
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
    if (s_rotating_reference_active)
    {
        if (!rotating_current_test_step(
                &s_rotating_reference,
                &current_a_reference_counts,
                &current_b_reference_counts) ||
            !phase_current_loop_set_reference_counts_prevalidated(
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
    }
    else if (s_phase_prediction_active)
    {
        if (!predict_aligned_reference(
                &s_phase_predictor,
                s_aligned_q_reference_counts,
                timebase_micros(),
                &predicted_phase_q32,
                &prediction_age_us,
                &current_a_reference_counts,
                &current_b_reference_counts,
                &rejection_reason))
        {
            record_prediction_rejection(
                rejection_reason, prediction_age_us);
            fault_from_interrupt(
                CURRENT_LOOP_BACKEND_FAULT_PHASE_PREDICTION);
            return;
        }
        if (!phase_current_loop_set_reference_counts_prevalidated(
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
        record_prediction_age(prediction_age_us);
    }
    if (!phase_current_loop_step_prevalidated(
            &s_loop,
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
    trace_armed = s_trace_armed;
    if (trace_armed)
    {
        pwm_stage_cycle_count = cycle_counter_read();
        if (!tim3_bridge_pwm_get_preload_margin_ticks(
                &pwm_preload_margin_ticks))
        {
            fault_from_interrupt(CURRENT_LOOP_BACKEND_FAULT_PWM);
            return;
        }
    }
    if (trace_armed &&
        (s_trace_count < CURRENT_LOOP_BACKEND_TRACE_CAPACITY) &&
        snapshot->timing_valid)
    {
        const uint16_t trace_index = s_trace_count;
        current_loop_backend_trace_sample_t* trace =
            &s_trace[trace_index];

        trace->loop_sample_count = s_sample_count + 1u;
        trace->current_a_reference_counts =
            s_last_reference_a_counts;
        trace->current_b_reference_counts =
            s_last_reference_b_counts;
        trace->current_a_measured_counts =
            output.current_a_measured_counts;
        trace->current_b_measured_counts =
            output.current_b_measured_counts;
        trace->phase_a_voltage_permille =
            output.phase_a_voltage_permille;
        trace->phase_b_voltage_permille =
            output.phase_b_voltage_permille;
        trace->predicted_electrical_phase_q32 =
            s_predicted_electrical_phase_q32;
        trace->phase_prediction_age_us =
            saturate_u16(s_phase_prediction_age_us);
        trace->trigger_timer_count = snapshot->trigger_timer_count;
        trace->trigger_to_dma_timer_ticks =
            snapshot->trigger_to_dma_timer_ticks;
        trace->dma_to_pwm_stage_cycles = saturate_u16(
            pwm_stage_cycle_count - snapshot->dma_entry_cycle_count);
        trace->pwm_preload_margin_ticks = pwm_preload_margin_ticks;
        trace->dma_to_trace_record_cycles = saturate_u16(
            cycle_counter_read() - snapshot->dma_entry_cycle_count);
        __DMB();
        s_trace_count = trace_index + 1u;
        if (s_trace_count == CURRENT_LOOP_BACKEND_TRACE_CAPACITY)
        {
            s_trace_armed = false;
            (void)adc1_set_current_timing_capture(false);
        }
    }
    s_latest_output = output;
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
        if (s_guardian_empty_updates != UINT32_MAX)
        {
            ++s_guardian_empty_updates;
        }
        if (s_guardian_missed_update_count != UINT32_MAX)
        {
            ++s_guardian_missed_update_count;
        }
        if (s_guardian_empty_updates >
            s_guardian_maximum_empty_updates)
        {
            s_guardian_maximum_empty_updates =
                s_guardian_empty_updates;
        }
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
    s_phase_predictor_config = *phase_predictor_config;
    s_fault_flags = CURRENT_LOOP_BACKEND_FAULT_NONE;
    s_sample_count = 0u;
    s_output_generation = 0u;
    s_last_reference_a_counts = 0;
    s_last_reference_b_counts = 0;
    s_aligned_q_reference_counts = 0;
    s_predicted_electrical_phase_q32 = 0u;
    s_phase_prediction_age_us = 0u;
    s_maximum_observed_phase_prediction_age_us = 0u;
    s_rejected_phase_prediction_age_us = 0u;
    s_phase_prediction_reject_reason =
        CURRENT_LOOP_PHASE_PREDICTION_REJECT_NONE;
    s_guardian_generation = 0u;
    s_guardian_empty_updates = 0u;
    s_guardian_missed_update_count = 0u;
    s_guardian_maximum_empty_updates = 0u;
    s_guardian_primed = false;
    s_trace_count = 0u;
    s_trace_armed = false;
    s_active = false;
    s_phase_prediction_active = false;
    s_rotating_reference_active = false;
    s_initialized = false;
    (void)adc1_set_current_timing_capture(false);

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
        s_rotating_reference_active = false;
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
        s_trace_armed = false;
        (void)adc1_set_current_timing_capture(false);
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

bool current_loop_backend_set_rotating_reference(
    int16_t amplitude_counts,
    uint32_t phase_increment_q32_per_step,
    uint32_t initial_phase_q32,
    uint64_t ramp_step_count)
{
    rotating_current_test_t candidate = {0};
    uint32_t previous;
    bool accepted = false;

    if (!s_initialized || s_active ||
        (s_fault_flags != CURRENT_LOOP_BACKEND_FAULT_NONE) ||
        ((uint16_t)amplitude_counts > s_config.reference_limit_counts) ||
        !rotating_current_test_init(
            &candidate,
            amplitude_counts,
            phase_increment_q32_per_step,
            initial_phase_q32,
            ramp_step_count))
    {
        return false;
    }

    previous = control_critical_enter();
    if (!s_active &&
        (s_fault_flags == CURRENT_LOOP_BACKEND_FAULT_NONE) &&
        phase_current_loop_set_reference_counts(
            &s_loop, &s_config, 0, 0))
    {
        s_rotating_reference = candidate;
        electrical_phase_predictor_reset(&s_phase_predictor);
        s_phase_prediction_active = false;
        s_rotating_reference_active = true;
        s_aligned_q_reference_counts = 0;
        s_predicted_electrical_phase_q32 = 0u;
        s_phase_prediction_age_us = 0u;
        s_last_reference_a_counts = 0;
        s_last_reference_b_counts = 0;
        accepted = true;
    }
    control_critical_exit(previous);
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
    current_loop_phase_prediction_reject_t rejection_reason =
        CURRENT_LOOP_PHASE_PREDICTION_REJECT_NONE;
    bool candidate_valid = true;
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
    if ((uint32_t)q_magnitude > s_config.reference_limit_counts)
    {
        candidate_valid = false;
        rejection_reason =
            CURRENT_LOOP_PHASE_PREDICTION_REJECT_REFERENCE_RANGE;
    }
    else if (!electrical_phase_predictor_set_observation(
                 &candidate,
                 electrical_phase_q32,
                 mechanical_velocity_revolutions_per_second_q16_16,
                 encoder_direction,
                 encoder_timestamp_us))
    {
        candidate_valid = false;
        rejection_reason =
            CURRENT_LOOP_PHASE_PREDICTION_REJECT_OBSERVATION_INVALID;
    }
    else if (!predict_aligned_reference(
                 &candidate,
                 q_current_reference_counts,
                 now_us,
                 &predicted_phase_q32,
                 &prediction_age_us,
                 &current_a_reference_counts,
                 &current_b_reference_counts,
                 &rejection_reason))
    {
        candidate_valid = false;
    }

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
            s_rotating_reference_active = false;
            s_aligned_q_reference_counts = q_current_reference_counts;
            s_predicted_electrical_phase_q32 = predicted_phase_q32;
            record_prediction_age(prediction_age_us);
            s_last_reference_a_counts = current_a_reference_counts;
            s_last_reference_b_counts = current_b_reference_counts;
            accepted = true;
        }
        else
        {
            const uint32_t phase_fault =
                s_loop.fault_flags & CURRENT_LOOP_BACKEND_FAULT_PHASE_MASK;

            if (!candidate_valid)
            {
                record_prediction_rejection(
                    rejection_reason, prediction_age_us);
            }
            s_active = false;
            s_trace_armed = false;
            (void)adc1_set_current_timing_capture(false);
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
    s_guardian_missed_update_count = 0u;
    s_guardian_maximum_empty_updates = 0u;
    s_guardian_primed = false;
    s_trace_count = 0u;
    s_trace_armed = false;
    (void)adc1_set_current_timing_capture(false);
    s_maximum_observed_phase_prediction_age_us = 0u;
    s_rejected_phase_prediction_age_us = 0u;
    s_phase_prediction_reject_reason =
        CURRENT_LOOP_PHASE_PREDICTION_REJECT_NONE;
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
    s_trace_armed = false;
    (void)adc1_set_current_timing_capture(false);
    (void)tim3_bridge_pwm_update_irq_enable(false);
    phase_current_loop_stop(&s_loop);
    electrical_phase_predictor_reset(&s_phase_predictor);
    s_phase_prediction_active = false;
    s_rotating_reference_active = false;
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

bool current_loop_backend_reconfigure_gains(
    int32_t proportional_gain_q16_per_count,
    int32_t integral_gain_q16_per_count_per_step)
{
    phase_current_loop_config_t candidate = s_config;
    phase_current_loop_t reset_loop;
    electrical_phase_predictor_t reset_predictor;
    uint32_t previous;
    bool accepted;

    candidate.proportional_gain_q16_per_count =
        proportional_gain_q16_per_count;
    candidate.integral_gain_q16_per_count_per_step =
        integral_gain_q16_per_count_per_step;
    if (!s_initialized ||
        !phase_current_loop_config_is_valid(&candidate) ||
        !phase_current_loop_init(&reset_loop, &candidate) ||
        !electrical_phase_predictor_init(
            &reset_predictor, &s_phase_predictor_config))
    {
        return false;
    }

    previous = control_critical_enter();
    if (s_active ||
        (s_fault_flags != CURRENT_LOOP_BACKEND_FAULT_NONE))
    {
        control_critical_exit(previous);
        return false;
    }

    /* Reconfiguration is an idle-only safety transaction. Re-establish the
       direct-GPIO ZERO vector before rebuilding the idle PWM binding; only
       publish the candidate gains after every backend step accepts it. */
    s_trace_armed = false;
    (void)adc1_set_current_timing_capture(false);
    (void)tim3_bridge_pwm_update_irq_enable(false);
    board_bridge_force_low_zero();
    accepted =
        board_bridge_pwm_init(platform_apb1_timer_clock_hz()) &&
        tim3_bridge_pwm_set_update_handler(pwm_update_event, NULL) &&
        tim3_bridge_pwm_zero();
    if (accepted)
    {
        s_config = candidate;
        s_loop = reset_loop;
        s_phase_predictor = reset_predictor;
        memset(&s_latest_output, 0, sizeof(s_latest_output));
        s_output_generation = 0u;
        s_last_reference_a_counts = 0;
        s_last_reference_b_counts = 0;
        s_aligned_q_reference_counts = 0;
        s_predicted_electrical_phase_q32 = 0u;
        s_phase_prediction_age_us = 0u;
        s_maximum_observed_phase_prediction_age_us = 0u;
        s_rejected_phase_prediction_age_us = 0u;
        s_phase_prediction_reject_reason =
            CURRENT_LOOP_PHASE_PREDICTION_REJECT_NONE;
        s_guardian_generation = 0u;
        s_guardian_empty_updates = 0u;
        s_guardian_missed_update_count = 0u;
        s_guardian_maximum_empty_updates = 0u;
        s_guardian_primed = false;
        s_trace_count = 0u;
        s_phase_prediction_active = false;
        s_rotating_reference_active = false;
    }
    else
    {
        board_bridge_force_low_zero();
        s_fault_flags |= CURRENT_LOOP_BACKEND_FAULT_INTERNAL;
    }
    control_critical_exit(previous);
    return accepted;
}

bool current_loop_backend_recover(uint32_t* cleared_fault_flags)
{
    uint32_t previous;
    uint32_t previous_faults;
    bool recovered;

    if (cleared_fault_flags != NULL)
    {
        *cleared_fault_flags = CURRENT_LOOP_BACKEND_FAULT_NONE;
    }
    if (!s_initialized)
    {
        board_bridge_force_low_zero();
        return false;
    }

    previous = control_critical_enter();
    previous_faults = s_fault_flags;

    /* Recovery is an operator-requested rearm, not a continuation. Tear the
       switching path back down to the direct-GPIO ZERO vector before
       rebuilding the idle PWM backend and all of its volatile loop state. */
    s_active = false;
    s_trace_armed = false;
    (void)adc1_set_current_timing_capture(false);
    (void)tim3_bridge_pwm_update_irq_enable(false);
    phase_current_loop_stop(&s_loop);
    board_bridge_force_low_zero();

    recovered =
        phase_current_loop_init(&s_loop, &s_config) &&
        electrical_phase_predictor_init(
            &s_phase_predictor, &s_phase_predictor_config) &&
        (adc1_restart_pwm_synchronized_current() == ADC1_STATUS_OK) &&
        adc1_set_current_event_handler(adc_current_event, NULL) &&
        board_bridge_pwm_init(platform_apb1_timer_clock_hz()) &&
        tim3_bridge_pwm_set_update_handler(pwm_update_event, NULL) &&
        tim3_bridge_pwm_zero();

    memset(&s_latest_output, 0, sizeof(s_latest_output));
    s_output_generation = 0u;
    s_last_reference_a_counts = 0;
    s_last_reference_b_counts = 0;
    s_aligned_q_reference_counts = 0;
    s_predicted_electrical_phase_q32 = 0u;
    s_phase_prediction_age_us = 0u;
    s_maximum_observed_phase_prediction_age_us = 0u;
    s_rejected_phase_prediction_age_us = 0u;
    s_phase_prediction_reject_reason =
        CURRENT_LOOP_PHASE_PREDICTION_REJECT_NONE;
    s_guardian_generation = 0u;
    s_guardian_empty_updates = 0u;
    s_guardian_missed_update_count = 0u;
    s_guardian_maximum_empty_updates = 0u;
    s_guardian_primed = false;
    s_trace_count = 0u;
    s_trace_armed = false;
    s_phase_prediction_active = false;
    s_rotating_reference_active = false;

    if (recovered)
    {
        s_fault_flags = CURRENT_LOOP_BACKEND_FAULT_NONE;
        if (cleared_fault_flags != NULL)
        {
            *cleared_fault_flags = previous_faults;
        }
    }
    else
    {
        board_bridge_force_low_zero();
        s_fault_flags = previous_faults |
            CURRENT_LOOP_BACKEND_FAULT_INTERNAL;
    }
    control_critical_exit(previous);
    return recovered;
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
    snapshot->maximum_observed_phase_prediction_age_us =
        s_maximum_observed_phase_prediction_age_us;
    snapshot->rejected_phase_prediction_age_us =
        s_rejected_phase_prediction_age_us;
    snapshot->missed_pwm_update_count =
        s_guardian_missed_update_count;
    snapshot->maximum_consecutive_missed_pwm_updates =
        s_guardian_maximum_empty_updates;
    snapshot->maximum_phase_prediction_age_us =
        s_phase_predictor.config.maximum_prediction_age_us;
    snapshot->phase_prediction_output_lead_us =
        s_phase_predictor.config.output_lead_us;
    snapshot->phase_prediction_reject_reason =
        s_phase_prediction_reject_reason;
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

bool current_loop_backend_trace_arm(void)
{
    uint32_t previous;

    if (!s_initialized)
    {
        return false;
    }

    previous = control_critical_enter();
    if (!s_active ||
        (s_fault_flags != CURRENT_LOOP_BACKEND_FAULT_NONE))
    {
        control_critical_exit(previous);
        return false;
    }
    s_trace_armed = false;
    (void)adc1_set_current_timing_capture(false);
    s_trace_count = 0u;
    if (!cycle_counter_init() ||
        !adc1_set_current_timing_capture(true))
    {
        control_critical_exit(previous);
        return false;
    }
    __DMB();
    s_trace_armed = true;
    control_critical_exit(previous);
    return true;
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
