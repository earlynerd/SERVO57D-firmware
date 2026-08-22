#include "mks57d/aligned_torque_controller.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#include "mks57d/phase_current_reference.h"

enum
{
    Q16_SHIFT = 16u,
    Q16_HALF = 1u << (Q16_SHIFT - 1u),
    QUARTER_CYCLE_PHASE = 0x40000000u,
    MICROSECONDS_PER_SECOND = 1000000u
};

static int64_t magnitude_i32(int32_t value)
{
    return (value < 0) ? -(int64_t)value : (int64_t)value;
}

static int16_t q16_16_to_i16(int32_t value)
{
    if (value >= 0)
    {
        return (int16_t)((value + (int32_t)Q16_HALF) >> Q16_SHIFT);
    }
    return (int16_t)(-
        ((-(int64_t)value + Q16_HALF) >> Q16_SHIFT));
}

static void clear_applied_reference(aligned_torque_controller_t* controller)
{
    controller->applied_q_current_q16_16 = 0;
    controller->status.applied_q_current_counts = 0;
    controller->status.current_a_reference_counts = 0;
    controller->status.current_b_reference_counts = 0;
    controller->status.active = false;
}

static aligned_torque_event_t fail_controller(
    aligned_torque_controller_t* controller,
    aligned_torque_result_t result,
    uint32_t fault,
    uint32_t now_millis)
{
    controller->status.state = ALIGNED_TORQUE_STATE_FAILED;
    controller->status.result = result;
    controller->status.fault_flags |= fault;
    controller->status.elapsed_millis = now_millis - controller->start_millis;
    clear_applied_reference(controller);
    return ALIGNED_TORQUE_EVENT_FAILED;
}

bool aligned_torque_config_is_valid(
    const aligned_torque_config_t* config)
{
    return (config != NULL) &&
           (config->maximum_current_counts > 0u) &&
           (config->maximum_current_counts <= INT16_MAX) &&
           (config->maximum_current_slew_counts_per_second > 0u) &&
           (config->maximum_velocity_revolutions_per_second_q16_16 > 0) &&
           (config->maximum_acceleration_revolutions_per_second2_q16_16 > 0) &&
           (config->maximum_feedback_interval_us > 0u) &&
           (config->minimum_duration_millis > 0u) &&
           (config->maximum_duration_millis >=
            config->minimum_duration_millis) &&
           (config->maximum_duration_millis <= (uint32_t)INT32_MAX);
}

bool aligned_torque_controller_init(
    aligned_torque_controller_t* controller,
    const aligned_torque_config_t* config)
{
    if ((controller == NULL) || !aligned_torque_config_is_valid(config))
    {
        return false;
    }

    memset(controller, 0, sizeof(*controller));
    controller->config = *config;
    controller->status.state = ALIGNED_TORQUE_STATE_IDLE;
    controller->initialized = true;
    return true;
}

static bool start_controller(
    aligned_torque_controller_t* controller,
    int16_t requested_q_current_counts,
    uint32_t duration_millis,
    uint32_t now_millis,
    uint32_t encoder_timestamp_us,
    int32_t velocity_revolutions_per_second_q16_16)
{
    if ((controller == NULL) || !controller->initialized ||
        controller->status.active ||
        (magnitude_i32(requested_q_current_counts) >
         controller->config.maximum_current_counts) ||
        (duration_millis < controller->config.minimum_duration_millis) ||
        (duration_millis > controller->config.maximum_duration_millis) ||
        (magnitude_i32(velocity_revolutions_per_second_q16_16) >
         controller->config.maximum_velocity_revolutions_per_second_q16_16))
    {
        return false;
    }

    memset(&controller->status, 0, sizeof(controller->status));
    controller->status.state = ALIGNED_TORQUE_STATE_RAMPING;
    controller->status.requested_q_current_counts =
        requested_q_current_counts;
    controller->status.velocity_revolutions_per_second_q16_16 =
        velocity_revolutions_per_second_q16_16;
    controller->status.active = true;
    controller->applied_q_current_q16_16 = 0;
    controller->start_millis = now_millis;
    controller->deadline_millis = now_millis + duration_millis;
    controller->last_encoder_timestamp_us = encoder_timestamp_us;
    controller->last_velocity_revolutions_per_second_q16_16 =
        velocity_revolutions_per_second_q16_16;
    return true;
}

bool aligned_torque_controller_start(
    aligned_torque_controller_t* controller,
    int16_t requested_q_current_counts,
    uint32_t duration_millis,
    uint32_t now_millis,
    uint32_t encoder_timestamp_us,
    int32_t velocity_revolutions_per_second_q16_16)
{
    return (requested_q_current_counts != 0) &&
           start_controller(
               controller,
               requested_q_current_counts,
               duration_millis,
               now_millis,
               encoder_timestamp_us,
               velocity_revolutions_per_second_q16_16);
}

bool aligned_torque_controller_start_tracking(
    aligned_torque_controller_t* controller,
    uint32_t duration_millis,
    uint32_t now_millis,
    uint32_t encoder_timestamp_us,
    int32_t velocity_revolutions_per_second_q16_16)
{
    return start_controller(
        controller,
        0,
        duration_millis,
        now_millis,
        encoder_timestamp_us,
        velocity_revolutions_per_second_q16_16);
}

bool aligned_torque_controller_set_target(
    aligned_torque_controller_t* controller,
    int16_t requested_q_current_counts)
{
    if ((controller == NULL) || !controller->initialized ||
        !controller->status.active ||
        (magnitude_i32(requested_q_current_counts) >
         controller->config.maximum_current_counts))
    {
        return false;
    }
    controller->status.requested_q_current_counts =
        requested_q_current_counts;
    return true;
}

aligned_torque_event_t aligned_torque_controller_update(
    aligned_torque_controller_t* controller,
    uint32_t now_millis,
    uint32_t encoder_timestamp_us,
    bool phase_valid,
    uint32_t electrical_phase_q32,
    int32_t velocity_revolutions_per_second_q16_16,
    bool backend_active)
{
    uint32_t elapsed_us;
    int64_t acceleration;
    int64_t maximum_delta_q16_16;
    int32_t target_q16_16;
    int64_t remaining_q16_16;

    if ((controller == NULL) || !controller->initialized ||
        !controller->status.active)
    {
        return ALIGNED_TORQUE_EVENT_NONE;
    }

    controller->status.elapsed_millis = now_millis - controller->start_millis;
    if (!backend_active)
    {
        return fail_controller(
            controller,
            ALIGNED_TORQUE_RESULT_BACKEND_INACTIVE,
            ALIGNED_TORQUE_FAULT_BACKEND_INACTIVE,
            now_millis);
    }
    if (!phase_valid)
    {
        return fail_controller(
            controller,
            ALIGNED_TORQUE_RESULT_PHASE_INVALID,
            ALIGNED_TORQUE_FAULT_PHASE_INVALID,
            now_millis);
    }

    elapsed_us = encoder_timestamp_us - controller->last_encoder_timestamp_us;
    if ((elapsed_us == 0u) ||
        (elapsed_us > controller->config.maximum_feedback_interval_us))
    {
        return fail_controller(
            controller,
            ALIGNED_TORQUE_RESULT_FEEDBACK_TIMING,
            ALIGNED_TORQUE_FAULT_FEEDBACK_TIMING,
            now_millis);
    }
    if (magnitude_i32(velocity_revolutions_per_second_q16_16) >
        controller->config.maximum_velocity_revolutions_per_second_q16_16)
    {
        return fail_controller(
            controller,
            ALIGNED_TORQUE_RESULT_OVERSPEED,
            ALIGNED_TORQUE_FAULT_OVERSPEED,
            now_millis);
    }

    acceleration =
        velocity_revolutions_per_second_q16_16 >=
                controller->last_velocity_revolutions_per_second_q16_16 ?
            (int64_t)velocity_revolutions_per_second_q16_16 -
                controller->last_velocity_revolutions_per_second_q16_16 :
            (int64_t)controller->last_velocity_revolutions_per_second_q16_16 -
                velocity_revolutions_per_second_q16_16;
    acceleration *= MICROSECONDS_PER_SECOND;
    acceleration = (acceleration + (elapsed_us / 2u)) / elapsed_us;
    if (acceleration > INT32_MAX)
    {
        controller->status.acceleration_revolutions_per_second2_q16_16 =
            INT32_MAX;
    }
    else
    {
        controller->status.acceleration_revolutions_per_second2_q16_16 =
            (int32_t)acceleration;
    }
    if (acceleration >
        controller->config.maximum_acceleration_revolutions_per_second2_q16_16)
    {
        return fail_controller(
            controller,
            ALIGNED_TORQUE_RESULT_OVERACCELERATION,
            ALIGNED_TORQUE_FAULT_OVERACCELERATION,
            now_millis);
    }
    if ((int32_t)(now_millis - controller->deadline_millis) >= 0)
    {
        controller->status.state = ALIGNED_TORQUE_STATE_COMPLETE;
        controller->status.result = ALIGNED_TORQUE_RESULT_DEADLINE;
        clear_applied_reference(controller);
        return ALIGNED_TORQUE_EVENT_COMPLETED;
    }

    target_q16_16 =
        (int32_t)controller->status.requested_q_current_counts *
        (int32_t)(1u << Q16_SHIFT);
    remaining_q16_16 =
        (int64_t)target_q16_16 - controller->applied_q_current_q16_16;
    maximum_delta_q16_16 =
        ((int64_t)controller->config.
             maximum_current_slew_counts_per_second *
         elapsed_us << Q16_SHIFT) /
        MICROSECONDS_PER_SECOND;
    if (magnitude_i32((int32_t)remaining_q16_16) <= maximum_delta_q16_16)
    {
        controller->applied_q_current_q16_16 = target_q16_16;
        controller->status.state = ALIGNED_TORQUE_STATE_HOLDING;
    }
    else if (remaining_q16_16 > 0)
    {
        controller->applied_q_current_q16_16 +=
            (int32_t)maximum_delta_q16_16;
    }
    else
    {
        controller->applied_q_current_q16_16 -=
            (int32_t)maximum_delta_q16_16;
    }

    controller->status.applied_q_current_counts =
        q16_16_to_i16(controller->applied_q_current_q16_16);
    controller->status.electrical_phase_q32 = electrical_phase_q32;
    controller->status.phase_valid = true;
    controller->status.velocity_revolutions_per_second_q16_16 =
        velocity_revolutions_per_second_q16_16;
    if (!phase_current_reference_from_polar(
            controller->status.applied_q_current_counts,
            electrical_phase_q32 + QUARTER_CYCLE_PHASE,
            &controller->status.current_a_reference_counts,
            &controller->status.current_b_reference_counts))
    {
        return fail_controller(
            controller,
            ALIGNED_TORQUE_RESULT_REFERENCE_REJECTED,
            ALIGNED_TORQUE_FAULT_REFERENCE_REJECTED,
            now_millis);
    }

    controller->last_encoder_timestamp_us = encoder_timestamp_us;
    controller->last_velocity_revolutions_per_second_q16_16 =
        velocity_revolutions_per_second_q16_16;
    return ALIGNED_TORQUE_EVENT_REFERENCE_CHANGED;
}

bool aligned_torque_controller_stop(
    aligned_torque_controller_t* controller,
    uint32_t now_millis)
{
    if ((controller == NULL) || !controller->initialized ||
        !controller->status.active)
    {
        return false;
    }

    controller->status.state = ALIGNED_TORQUE_STATE_STOPPED;
    controller->status.result = ALIGNED_TORQUE_RESULT_STOPPED;
    controller->status.elapsed_millis = now_millis - controller->start_millis;
    clear_applied_reference(controller);
    return true;
}

bool aligned_torque_controller_reference_rejected(
    aligned_torque_controller_t* controller,
    uint32_t now_millis)
{
    if ((controller == NULL) || !controller->initialized ||
        !controller->status.active)
    {
        return false;
    }
    (void)fail_controller(
        controller,
        ALIGNED_TORQUE_RESULT_REFERENCE_REJECTED,
        ALIGNED_TORQUE_FAULT_REFERENCE_REJECTED,
        now_millis);
    return true;
}

bool aligned_torque_controller_is_active(
    const aligned_torque_controller_t* controller)
{
    return (controller != NULL) && controller->initialized &&
           controller->status.active;
}

void aligned_torque_controller_get_status(
    const aligned_torque_controller_t* controller,
    aligned_torque_status_t* status)
{
    const aligned_torque_status_t empty = {0};

    if (status == NULL)
    {
        return;
    }
    *status = ((controller != NULL) && controller->initialized) ?
        controller->status : empty;
}
