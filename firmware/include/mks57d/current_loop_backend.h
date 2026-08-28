#ifndef MKS57D_CURRENT_LOOP_BACKEND_H
#define MKS57D_CURRENT_LOOP_BACKEND_H

#include <stdbool.h>
#include <stdint.h>

#include "mks57d/electrical_phase_predictor.h"
#include "mks57d/phase_current_loop.h"

enum
{
    CURRENT_LOOP_BACKEND_TRACE_CAPACITY = 256u
};

typedef enum
{
    CURRENT_LOOP_BACKEND_FAULT_NONE = 0u,
    CURRENT_LOOP_BACKEND_FAULT_PHASE_MASK = 0x0000FFFFu,
    CURRENT_LOOP_BACKEND_FAULT_ADC = 1u << 16,
    CURRENT_LOOP_BACKEND_FAULT_PWM = 1u << 17,
    CURRENT_LOOP_BACKEND_FAULT_DEADLINE = 1u << 18,
    CURRENT_LOOP_BACKEND_FAULT_INTERNAL = 1u << 19,
    CURRENT_LOOP_BACKEND_FAULT_PHASE_PREDICTION = 1u << 20
} current_loop_backend_fault_t;

typedef enum
{
    CURRENT_LOOP_PHASE_PREDICTION_REJECT_NONE = 0u,
    CURRENT_LOOP_PHASE_PREDICTION_REJECT_OBSERVATION_INVALID = 1u,
    CURRENT_LOOP_PHASE_PREDICTION_REJECT_STALE = 2u,
    CURRENT_LOOP_PHASE_PREDICTION_REJECT_REFERENCE_RANGE = 3u,
    CURRENT_LOOP_PHASE_PREDICTION_REJECT_REFERENCE_MAPPING = 4u
} current_loop_phase_prediction_reject_t;

typedef enum
{
    CURRENT_LOOP_BACKEND_CONTROLLER_STATIONARY = 0u,
    CURRENT_LOOP_BACKEND_CONTROLLER_ROTATING_FRAME = 1u,
    CURRENT_LOOP_BACKEND_CONTROLLER_COUNT
} current_loop_backend_controller_t;

typedef struct
{
    phase_current_loop_output_t latest_output;
    int16_t current_a_reference_counts;
    int16_t current_b_reference_counts;
    uint32_t sample_count;
    uint32_t fault_flags;
    uint32_t predicted_electrical_phase_q32;
    int32_t electrical_phase_rate_q32_per_us;
    uint32_t phase_prediction_age_us;
    uint32_t maximum_observed_phase_prediction_age_us;
    uint32_t rejected_phase_prediction_age_us;
    uint32_t missed_pwm_update_count;
    uint32_t maximum_consecutive_missed_pwm_updates;
    uint32_t maximum_phase_prediction_age_us;
    uint16_t phase_prediction_output_lead_us;
    uint8_t phase_prediction_reject_reason;
    uint8_t rotating_reference_controller_mode;
    bool initialized;
    bool active;
    bool phase_prediction_active;
} current_loop_backend_snapshot_t;

typedef struct
{
    uint32_t loop_sample_count;
    int16_t current_a_reference_counts;
    int16_t current_b_reference_counts;
    int16_t current_a_measured_counts;
    int16_t current_b_measured_counts;
    int16_t phase_a_voltage_permille;
    int16_t phase_b_voltage_permille;
    uint32_t predicted_electrical_phase_q32;
    uint16_t phase_prediction_age_us;
    uint16_t trigger_timer_count;
    uint16_t trigger_to_dma_timer_ticks;
    uint16_t dma_to_pwm_stage_cycles;
    uint16_t dma_to_trace_record_cycles;
    uint16_t pwm_preload_margin_ticks;
} current_loop_backend_trace_sample_t;

bool current_loop_backend_init(
    const phase_current_loop_config_t* config,
    const electrical_phase_predictor_config_t* phase_predictor_config);
bool current_loop_backend_set_reference_counts(
    int16_t current_a_reference_counts,
    int16_t current_b_reference_counts);
bool current_loop_backend_set_rotating_reference(
    int16_t amplitude_counts,
    uint32_t phase_increment_q32_per_step,
    uint32_t initial_phase_q32,
    uint64_t ramp_step_count,
    current_loop_backend_controller_t controller_mode);
bool current_loop_backend_set_aligned_q_reference(
    int16_t q_current_reference_counts,
    uint32_t electrical_phase_q32,
    int32_t mechanical_velocity_revolutions_per_second_q16_16,
    int8_t encoder_direction,
    uint32_t encoder_timestamp_us);
bool current_loop_backend_start(void);
bool current_loop_backend_stop(void);
bool current_loop_backend_reconfigure_gains(
    int32_t proportional_gain_q16_per_count,
    int32_t integral_gain_q16_per_count_per_step);
bool current_loop_backend_recover(uint32_t* cleared_fault_flags);
void current_loop_backend_get_snapshot(
    current_loop_backend_snapshot_t* snapshot);
uint32_t current_loop_backend_sample_count(void);
uint16_t current_loop_backend_trace_count(void);
bool current_loop_backend_trace_arm(void);
bool current_loop_backend_trace_get(
    uint16_t index,
    current_loop_backend_trace_sample_t* sample);

#endif
