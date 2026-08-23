#ifndef MKS57D_PHASE_CURRENT_LOOP_H
#define MKS57D_PHASE_CURRENT_LOOP_H

#include <stdbool.h>
#include <stdint.h>

enum
{
    PHASE_CURRENT_LOOP_CHANNEL_COUNT = 4u,
    PHASE_CURRENT_LOOP_DUTY_FULL_SCALE_PERMILLE = 1000u,
    PHASE_CURRENT_LOOP_Q16_SHIFT = 16u,
    PHASE_CURRENT_LOOP_Q16_ONE = 1u << PHASE_CURRENT_LOOP_Q16_SHIFT,
    PHASE_CURRENT_LOOP_PROPORTIONAL_GAIN_MAXIMUM_Q16 =
        16 * PHASE_CURRENT_LOOP_Q16_ONE,
    PHASE_CURRENT_LOOP_INTEGRAL_GAIN_MAXIMUM_Q16 =
        PHASE_CURRENT_LOOP_Q16_ONE
};

typedef enum
{
    PHASE_CURRENT_LOOP_FAULT_NONE = 0u,
    PHASE_CURRENT_LOOP_FAULT_INVALID_SAMPLE = 1u << 0,
    PHASE_CURRENT_LOOP_FAULT_OVERCURRENT_A = 1u << 1,
    PHASE_CURRENT_LOOP_FAULT_OVERCURRENT_B = 1u << 2,
    PHASE_CURRENT_LOOP_FAULT_INVALID_REFERENCE = 1u << 3,
    PHASE_CURRENT_LOOP_FAULT_INVALID_OUTPUT = 1u << 4
} phase_current_loop_fault_t;

typedef struct
{
    uint16_t current_a_zero_raw;
    uint16_t current_b_zero_raw;
    uint16_t reference_limit_counts;
    uint16_t hard_current_limit_counts;
    int32_t proportional_gain_q16_per_count;
    int32_t integral_gain_q16_per_count_per_step;
    uint16_t phase_voltage_limit_permille;
    uint16_t duty_margin_permille;
    int8_t current_a_polarity;
    int8_t current_b_polarity;
} phase_current_loop_config_t;

typedef struct
{
    int32_t current_a_integrator_q16;
    int32_t current_b_integrator_q16;
    int16_t current_a_reference_counts;
    int16_t current_b_reference_counts;
    uint32_t fault_flags;
    bool initialized;
    bool running;
} phase_current_loop_t;

typedef struct
{
    int16_t current_a_measured_counts;
    int16_t current_b_measured_counts;
    int16_t phase_a_voltage_permille;
    int16_t phase_b_voltage_permille;
    uint16_t duty_permille[PHASE_CURRENT_LOOP_CHANNEL_COUNT];
} phase_current_loop_output_t;

bool phase_current_loop_config_is_valid(
    const phase_current_loop_config_t* config);
bool phase_current_loop_init(phase_current_loop_t* loop,
                             const phase_current_loop_config_t* config);
bool phase_current_loop_set_reference_counts(
    phase_current_loop_t* loop,
    const phase_current_loop_config_t* config,
    int16_t current_a_reference_counts,
    int16_t current_b_reference_counts);
/* Current-backend hot-path variant. The caller must have validated config and
 * must keep it immutable until the loop stops. Runtime state, fault, and
 * reference-limit checks remain enforced. */
bool phase_current_loop_set_reference_counts_prevalidated(
    phase_current_loop_t* loop,
    const phase_current_loop_config_t* config,
    int16_t current_a_reference_counts,
    int16_t current_b_reference_counts);
bool phase_current_loop_start(phase_current_loop_t* loop);
void phase_current_loop_stop(phase_current_loop_t* loop);
bool phase_current_loop_step(phase_current_loop_t* loop,
                             const phase_current_loop_config_t* config,
                             uint16_t current_a_raw,
                             uint16_t current_b_raw,
                             phase_current_loop_output_t* output);
/* Current-backend hot-path variant with the same prevalidated-config contract
 * as phase_current_loop_set_reference_counts_prevalidated(). Raw ADC,
 * overcurrent, output, runtime-state, and fault checks remain enforced. */
bool phase_current_loop_step_prevalidated(
    phase_current_loop_t* loop,
    const phase_current_loop_config_t* config,
    uint16_t current_a_raw,
    uint16_t current_b_raw,
    phase_current_loop_output_t* output);

#endif
