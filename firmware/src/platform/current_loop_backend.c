#include "mks57d/current_loop_backend.h"

#include <stddef.h>
#include <string.h>

#include "mks57d/adc1.h"
#include "mks57d/board.h"
#include "mks57d/interrupt_priority.h"
#include "mks57d/tim3_bridge_pwm.h"
#include "n32l40x.h"

enum
{
    NVIC_PRIORITY_SHIFT = 8u - __NVIC_PRIO_BITS,
    MAX_CONSECUTIVE_EMPTY_PWM_UPDATES = 1u
};

static phase_current_loop_config_t s_config;
static phase_current_loop_t s_loop;
static volatile uint32_t s_fault_flags;
static volatile uint32_t s_sample_count;
static volatile uint32_t s_output_generation;
static int16_t s_last_reference_a_counts;
static int16_t s_last_reference_b_counts;
static uint32_t s_guardian_generation;
static uint32_t s_guardian_empty_updates;
static volatile bool s_initialized;
static volatile bool s_active;
static bool s_guardian_primed;
static phase_current_loop_output_t s_latest_output;

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

static void adc_current_event(adc1_status_t status,
                              const adc1_current_snapshot_t* snapshot,
                              void* context)
{
    phase_current_loop_output_t output;

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
    const phase_current_loop_config_t* config)
{
    if (s_initialized || !phase_current_loop_config_is_valid(config))
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
    s_guardian_generation = 0u;
    s_guardian_empty_updates = 0u;
    s_guardian_primed = false;
    s_active = false;
    s_initialized = false;

    if (!phase_current_loop_init(&s_loop, &s_config) ||
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
    snapshot->initialized = s_initialized;
    snapshot->active = s_active;
    control_critical_exit(previous);
}
