#ifndef MKS57D_ALIGNED_TORQUE_CONTROLLER_H
#define MKS57D_ALIGNED_TORQUE_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    ALIGNED_TORQUE_STATE_IDLE = 0,
    ALIGNED_TORQUE_STATE_RAMPING,
    ALIGNED_TORQUE_STATE_HOLDING,
    ALIGNED_TORQUE_STATE_COMPLETE,
    ALIGNED_TORQUE_STATE_STOPPED,
    ALIGNED_TORQUE_STATE_FAILED
} aligned_torque_state_t;

typedef enum
{
    ALIGNED_TORQUE_RESULT_NONE = 0,
    ALIGNED_TORQUE_RESULT_DEADLINE,
    ALIGNED_TORQUE_RESULT_STOPPED,
    ALIGNED_TORQUE_RESULT_PHASE_INVALID,
    ALIGNED_TORQUE_RESULT_FEEDBACK_TIMING,
    ALIGNED_TORQUE_RESULT_OVERSPEED,
    ALIGNED_TORQUE_RESULT_OVERACCELERATION,
    ALIGNED_TORQUE_RESULT_BACKEND_INACTIVE,
    ALIGNED_TORQUE_RESULT_REFERENCE_REJECTED
} aligned_torque_result_t;

typedef enum
{
    ALIGNED_TORQUE_EVENT_NONE = 0,
    ALIGNED_TORQUE_EVENT_REFERENCE_CHANGED,
    ALIGNED_TORQUE_EVENT_COMPLETED,
    ALIGNED_TORQUE_EVENT_FAILED
} aligned_torque_event_t;

enum
{
    ALIGNED_TORQUE_FAULT_PHASE_INVALID = 1u << 0,
    ALIGNED_TORQUE_FAULT_FEEDBACK_TIMING = 1u << 1,
    ALIGNED_TORQUE_FAULT_OVERSPEED = 1u << 2,
    ALIGNED_TORQUE_FAULT_OVERACCELERATION = 1u << 3,
    ALIGNED_TORQUE_FAULT_BACKEND_INACTIVE = 1u << 4,
    ALIGNED_TORQUE_FAULT_REFERENCE_REJECTED = 1u << 5
};

typedef struct
{
    uint16_t maximum_current_counts;
    uint16_t maximum_current_slew_counts_per_second;
    int32_t maximum_velocity_revolutions_per_second_q16_16;
    int32_t maximum_acceleration_revolutions_per_second2_q16_16;
    uint16_t maximum_feedback_interval_us;
    uint32_t minimum_duration_millis;
    /* Must not exceed INT32_MAX for wrap-safe deadline comparisons. */
    uint32_t maximum_duration_millis;
} aligned_torque_config_t;

typedef struct
{
    aligned_torque_state_t state;
    aligned_torque_result_t result;
    uint32_t fault_flags;
    int16_t requested_q_current_counts;
    int16_t applied_q_current_counts;
    int16_t current_a_reference_counts;
    int16_t current_b_reference_counts;
    uint32_t electrical_phase_q32;
    int32_t velocity_revolutions_per_second_q16_16;
    int32_t acceleration_revolutions_per_second2_q16_16;
    uint32_t elapsed_millis;
    bool phase_valid;
    bool active;
} aligned_torque_status_t;

typedef struct
{
    aligned_torque_config_t config;
    aligned_torque_status_t status;
    int32_t applied_q_current_q16_16;
    uint32_t start_millis;
    uint32_t deadline_millis;
    uint32_t last_encoder_timestamp_us;
    int32_t last_velocity_revolutions_per_second_q16_16;
    bool initialized;
} aligned_torque_controller_t;

bool aligned_torque_config_is_valid(
    const aligned_torque_config_t* config);
bool aligned_torque_controller_init(
    aligned_torque_controller_t* controller,
    const aligned_torque_config_t* config);
bool aligned_torque_controller_start(
    aligned_torque_controller_t* controller,
    int16_t requested_q_current_counts,
    uint32_t duration_millis,
    uint32_t now_millis,
    uint32_t encoder_timestamp_us,
    int32_t velocity_revolutions_per_second_q16_16);
aligned_torque_event_t aligned_torque_controller_update(
    aligned_torque_controller_t* controller,
    uint32_t now_millis,
    uint32_t encoder_timestamp_us,
    bool phase_valid,
    uint32_t electrical_phase_q32,
    int32_t velocity_revolutions_per_second_q16_16,
    bool backend_active);
bool aligned_torque_controller_stop(
    aligned_torque_controller_t* controller,
    uint32_t now_millis);
bool aligned_torque_controller_reference_rejected(
    aligned_torque_controller_t* controller,
    uint32_t now_millis);
bool aligned_torque_controller_is_active(
    const aligned_torque_controller_t* controller);
void aligned_torque_controller_get_status(
    const aligned_torque_controller_t* controller,
    aligned_torque_status_t* status);

#endif
