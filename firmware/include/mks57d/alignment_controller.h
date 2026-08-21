#ifndef MKS57D_ALIGNMENT_CONTROLLER_H
#define MKS57D_ALIGNMENT_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>

#include "mks57d/motor_alignment.h"

typedef enum
{
    ALIGNMENT_CONTROLLER_STATE_IDLE = 0,
    ALIGNMENT_CONTROLLER_STATE_PHASE_ZERO_SETTLE,
    ALIGNMENT_CONTROLLER_STATE_PHASE_ZERO_SAMPLE,
    ALIGNMENT_CONTROLLER_STATE_PHASE_QUARTER_SETTLE,
    ALIGNMENT_CONTROLLER_STATE_PHASE_QUARTER_SAMPLE,
    ALIGNMENT_CONTROLLER_STATE_RETURN_ZERO_SETTLE,
    ALIGNMENT_CONTROLLER_STATE_RETURN_ZERO_SAMPLE,
    ALIGNMENT_CONTROLLER_STATE_COMPLETE,
    ALIGNMENT_CONTROLLER_STATE_FAILED,
    ALIGNMENT_CONTROLLER_STATE_ABORTED
} alignment_controller_state_t;

typedef enum
{
    ALIGNMENT_CONTROLLER_RESULT_NONE = 0,
    ALIGNMENT_CONTROLLER_RESULT_SUCCESS,
    ALIGNMENT_CONTROLLER_RESULT_ABORTED,
    ALIGNMENT_CONTROLLER_RESULT_DEADLINE,
    ALIGNMENT_CONTROLLER_RESULT_ENCODER_INVALID,
    ALIGNMENT_CONTROLLER_RESULT_BACKEND_INACTIVE,
    ALIGNMENT_CONTROLLER_RESULT_CURRENT_TRACKING,
    ALIGNMENT_CONTROLLER_RESULT_ENCODER_UNSTABLE,
    ALIGNMENT_CONTROLLER_RESULT_GEOMETRY,
    ALIGNMENT_CONTROLLER_RESULT_CLOSURE
} alignment_controller_result_t;

typedef enum
{
    ALIGNMENT_CONTROLLER_EVENT_NONE = 0,
    ALIGNMENT_CONTROLLER_EVENT_REFERENCE_CHANGED,
    ALIGNMENT_CONTROLLER_EVENT_COMPLETED,
    ALIGNMENT_CONTROLLER_EVENT_FAILED
} alignment_controller_event_t;

typedef struct
{
    uint32_t settle_duration_millis;
    uint32_t sample_duration_millis;
    uint32_t maximum_duration_millis;
    uint16_t minimum_sample_count;
    uint16_t maximum_sample_span_counts;
    uint16_t maximum_closure_error_counts;
    uint16_t maximum_current_error_counts;
} alignment_controller_config_t;

typedef struct
{
    alignment_controller_state_t state;
    alignment_controller_result_t result;
    uint16_t alignment_current_counts;
    uint16_t phase_zero_raw;
    uint16_t phase_quarter_raw;
    uint16_t return_zero_raw;
    uint16_t observed_quarter_step_counts;
    int16_t quarter_step_error_counts;
    int16_t closure_error_counts;
    int8_t encoder_direction;
    uint16_t active_sample_count;
    uint32_t elapsed_millis;
} alignment_controller_status_t;

typedef struct
{
    uint16_t origin_raw;
    int32_t minimum_delta_counts;
    int32_t maximum_delta_counts;
    int64_t delta_sum_counts;
    uint16_t sample_count;
    bool initialized;
} alignment_controller_sampler_t;

typedef struct
{
    alignment_controller_config_t config;
    alignment_controller_status_t status;
    alignment_controller_sampler_t sampler;
    motor_alignment_t* alignment;
    uint32_t operation_start_millis;
    uint32_t stage_start_millis;
    int16_t current_a_reference_counts;
    int16_t current_b_reference_counts;
    bool initialized;
} alignment_controller_t;

bool alignment_controller_config_is_valid(
    const alignment_controller_config_t* config);
bool alignment_controller_init(
    alignment_controller_t* controller,
    const alignment_controller_config_t* config);
bool alignment_controller_start(
    alignment_controller_t* controller,
    motor_alignment_t* alignment,
    uint16_t alignment_current_counts,
    uint32_t now_millis);
alignment_controller_event_t alignment_controller_update(
    alignment_controller_t* controller,
    uint32_t now_millis,
    bool encoder_valid,
    uint16_t encoder_raw,
    int16_t current_a_measured_counts,
    int16_t current_b_measured_counts,
    bool backend_active);
void alignment_controller_abort(alignment_controller_t* controller,
                                uint32_t now_millis);
bool alignment_controller_is_active(
    const alignment_controller_t* controller);
bool alignment_controller_get_reference_counts(
    const alignment_controller_t* controller,
    int16_t* current_a_reference_counts,
    int16_t* current_b_reference_counts);
void alignment_controller_get_status(
    const alignment_controller_t* controller,
    alignment_controller_status_t* status);

#endif
