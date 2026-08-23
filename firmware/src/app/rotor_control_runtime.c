#include "mks57d/rotor_control_runtime.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include "mks57d/board.h"
#include "mks57d/control_math.h"
#include "mks57d/current_loop_backend.h"
#include "mks57d/interrupt_priority.h"
#include "mks57d/mt6816.h"
#include "mks57d/timebase.h"
#include "n32l40x.h"

enum
{
    ROTOR_CONTROL_REQUEST_ALIGNMENT = 1u << 0,
    ROTOR_CONTROL_REQUEST_TORQUE = 1u << 1,
    ROTOR_CONTROL_REQUEST_STOP = 1u << 2,
    ROTOR_CONTROL_REQUEST_VELOCITY = 1u << 3,
    ROTOR_CONTROL_REQUEST_POSITION = 1u << 4,
    ROTOR_CONTROL_ESTIMATOR_FAULT_INVALID_SAMPLE = 1u << 0,
    ROTOR_CONTROL_MT6816_RESPONSE_LENGTH = 4u,
    ROTOR_CONTROL_FULL_SNAPSHOT_PERIOD_US = 10000u,
    NVIC_PRIORITY_SHIFT = 8u - __NVIC_PRIO_BITS
};

_Static_assert(sizeof(rotor_control_progress_snapshot_t) <= 64u,
               "rotor progress publication must remain compact");

static uint32_t runtime_critical_enter(void)
{
    const uint32_t previous = __get_BASEPRI();
    const uint32_t threshold =
        (uint32_t)INTERRUPT_PRIORITY_SLOW_RELEASE << NVIC_PRIORITY_SHIFT;

    __set_BASEPRI_MAX(threshold);
    __DSB();
    __ISB();
    return previous;
}

static void runtime_critical_exit(uint32_t previous)
{
    __set_BASEPRI(previous);
    __DSB();
    __ISB();
}

static bool publish_aligned_q_reference(
    const rotor_control_runtime_t* runtime,
    const aligned_torque_status_t* status,
    uint32_t encoder_timestamp_us)
{
    return (runtime != NULL) && (status != NULL) &&
           current_loop_backend_set_aligned_q_reference(
               status->applied_q_current_counts,
               status->electrical_phase_q32,
               status->velocity_revolutions_per_second_q16_16,
               runtime->motor_alignment.status.encoder_direction,
               encoder_timestamp_us);
}

static bool runtime_active(const rotor_control_runtime_t* runtime)
{
    return alignment_controller_is_active(&runtime->alignment_controller) ||
           aligned_torque_controller_is_active(&runtime->torque_controller) ||
           velocity_controller_is_active(&runtime->velocity_controller) ||
           position_controller_is_active(&runtime->position_controller);
}

static rotor_observation_t runtime_observation(
    const rotor_control_runtime_t* runtime)
{
    const rotor_observation_t observation = {
        .position_revolutions =
            runtime->angle_tracker.position_revolutions,
        .velocity_revolutions_per_second =
            runtime->angle_tracker.velocity_revolutions_per_second,
        .timestamp_us = runtime->angle_tracker.last_timestamp_us,
        .valid = runtime->angle_tracker.initialized &&
            (runtime->encoder_diagnostics.status ==
             (uint32_t)MT6816_STATUS_OK) &&
            (runtime->encoder_diagnostics.transport_status ==
             (uint32_t)SPI_STATUS_OK) &&
            (runtime->encoder_diagnostics.flags == 0u) &&
            (runtime->estimator_fault_flags == 0u),
    };

    return observation;
}

static uint32_t runtime_active_control_flags(
    const rotor_control_runtime_t* runtime)
{
    uint32_t flags = ROTOR_CONTROL_ACTIVE_NONE;

    if (alignment_controller_is_active(&runtime->alignment_controller))
    {
        flags |= ROTOR_CONTROL_ACTIVE_ALIGNMENT;
    }
    if (aligned_torque_controller_is_active(&runtime->torque_controller))
    {
        flags |= ROTOR_CONTROL_ACTIVE_ALIGNED_TORQUE;
    }
    if (velocity_controller_is_active(&runtime->velocity_controller))
    {
        flags |= ROTOR_CONTROL_ACTIVE_VELOCITY;
    }
    if (position_controller_is_active(&runtime->position_controller))
    {
        flags |= ROTOR_CONTROL_ACTIVE_POSITION;
    }
    return flags;
}

static void publish_progress_snapshot(rotor_control_runtime_t* runtime)
{
    ++runtime->progress_sequence;
    __DMB();
    runtime->progress_published.encoder_diagnostics =
        runtime->encoder_diagnostics;
    runtime->progress_published.estimator_position_revolutions =
        runtime->angle_tracker.position_revolutions;
    runtime->progress_published.estimator_velocity_revolutions_per_second =
        runtime->angle_tracker.velocity_revolutions_per_second;
    runtime->progress_published.estimator_timestamp_us =
        runtime->angle_tracker.last_timestamp_us;
    runtime->progress_published.estimator_fault_flags =
        runtime->estimator_fault_flags;
    runtime->progress_published.active_control_flags =
        runtime_active_control_flags(runtime);
    runtime->progress_published.estimator_initialized =
        runtime->angle_tracker.initialized ? 1u : 0u;
    runtime->progress_published.full_snapshot_sequence =
        runtime->snapshot_sequence;
    __DMB();
    ++runtime->progress_sequence;
}

static void publish_full_snapshot(rotor_control_runtime_t* runtime)
{
    ++runtime->snapshot_sequence;
    __DMB();
    runtime->published.encoder_diagnostics = runtime->encoder_diagnostics;
    runtime->published.observation = runtime_observation(runtime);
    runtime->published.angle_tracker = runtime->angle_tracker;
    runtime->published.motor_alignment = runtime->motor_alignment;
    runtime->published.alignment_controller = runtime->alignment_controller;
    runtime->published.torque_controller = runtime->torque_controller;
    runtime->published.velocity_controller = runtime->velocity_controller;
    runtime->published.position_controller = runtime->position_controller;
    runtime->published.estimator_fault_flags =
        runtime->estimator_fault_flags;
    runtime->published.estimator_sample_interval_us =
        runtime->estimator_sample_interval_us;
    runtime->published.estimator_maximum_sample_interval_us =
        runtime->estimator_maximum_sample_interval_us;
    __DMB();
    ++runtime->snapshot_sequence;
}

static void publish_callback_state(rotor_control_runtime_t* runtime,
                                   uint32_t timestamp_us,
                                   bool force_full_snapshot)
{
    /* Full controller state is telemetry; progress remains per acquisition. */
    if (force_full_snapshot ||
        !runtime->full_snapshot_timestamp_valid ||
        ((timestamp_us - runtime->last_full_snapshot_timestamp_us) >=
         ROTOR_CONTROL_FULL_SNAPSHOT_PERIOD_US))
    {
        publish_full_snapshot(runtime);
        runtime->last_full_snapshot_timestamp_us = timestamp_us;
        runtime->full_snapshot_timestamp_valid = true;
    }
    publish_progress_snapshot(runtime);
}

static bool stop_backend(rotor_control_runtime_t* runtime,
                         uint32_t event)
{
    current_loop_backend_snapshot_t loop = {0};

    if (!current_loop_backend_stop())
    {
        board_bridge_force_low_zero();
        runtime->event_flags |= ROTOR_CONTROL_EVENT_FAULT;
        return false;
    }
    current_loop_backend_get_snapshot(&loop);
    if (loop.fault_flags != 0u)
    {
        board_bridge_force_low_zero();
        runtime->event_flags |= ROTOR_CONTROL_EVENT_FAULT;
        return false;
    }
    runtime->event_flags |= event;
    return true;
}

static void fail_active_control(rotor_control_runtime_t* runtime,
                                uint32_t now_millis,
                                uint32_t timestamp_us)
{
    current_loop_backend_snapshot_t loop = {0};

    if (!runtime_active(runtime))
    {
        return;
    }
    if (velocity_controller_is_active(&runtime->velocity_controller))
    {
        (void)velocity_controller_actuator_failed(
            &runtime->velocity_controller, now_millis);
    }
    if (position_controller_is_active(&runtime->position_controller))
    {
        (void)position_controller_actuator_failed(
            &runtime->position_controller, now_millis);
    }
    alignment_controller_abort(&runtime->alignment_controller, now_millis);
    current_loop_backend_get_snapshot(&loop);
    if (aligned_torque_controller_is_active(&runtime->torque_controller))
    {
        (void)aligned_torque_controller_update(
            &runtime->torque_controller,
            now_millis,
            timestamp_us,
            false,
            0u,
            0,
            loop.active);
    }
    (void)current_loop_backend_stop();
    board_bridge_force_low_zero();
    runtime->event_flags |= ROTOR_CONTROL_EVENT_FAULT;
}

static void process_stop_request(rotor_control_runtime_t* runtime,
                                 uint32_t now_millis)
{
    bool stopped = false;

    if (alignment_controller_is_active(&runtime->alignment_controller))
    {
        alignment_controller_abort(&runtime->alignment_controller,
                                   now_millis);
        stopped = true;
    }
    if (position_controller_is_active(&runtime->position_controller))
    {
        (void)position_controller_stop(
            &runtime->position_controller, now_millis);
        stopped = true;
    }
    if (velocity_controller_is_active(&runtime->velocity_controller))
    {
        (void)velocity_controller_stop(
            &runtime->velocity_controller, now_millis);
        stopped = true;
    }
    if (aligned_torque_controller_is_active(&runtime->torque_controller))
    {
        (void)aligned_torque_controller_stop(
            &runtime->torque_controller, now_millis);
        stopped = true;
    }
    if (stopped)
    {
        (void)stop_backend(
            runtime, ROTOR_CONTROL_EVENT_AUTHORITY_RELEASED);
    }
    else
    {
        /* A stop can cancel a queued start before the controller becomes
         * active. Foreground may already have granted motion authority, so
         * always return the matching release acknowledgement. */
        runtime->event_flags |=
            ROTOR_CONTROL_EVENT_AUTHORITY_RELEASED;
    }
}

static void reject_requests_without_feedback(
    rotor_control_runtime_t* runtime,
    uint32_t now_millis,
    uint32_t timestamp_us)
{
    const uint32_t requests = runtime->request_flags;

    runtime->request_flags = 0u;
    fail_active_control(runtime, now_millis, timestamp_us);
    if ((requests & ROTOR_CONTROL_REQUEST_STOP) != 0u)
    {
        process_stop_request(runtime, now_millis);
    }
    else if ((requests & (ROTOR_CONTROL_REQUEST_ALIGNMENT |
                          ROTOR_CONTROL_REQUEST_TORQUE |
                          ROTOR_CONTROL_REQUEST_VELOCITY |
                          ROTOR_CONTROL_REQUEST_POSITION)) != 0u)
    {
        (void)current_loop_backend_stop();
        board_bridge_force_low_zero();
        runtime->event_flags |= ROTOR_CONTROL_EVENT_FAULT;
    }
}

static bool start_alignment(rotor_control_runtime_t* runtime,
                            uint32_t now_millis)
{
    current_loop_backend_snapshot_t loop = {0};
    int16_t current_a_reference_counts;
    int16_t current_b_reference_counts;

    current_loop_backend_get_snapshot(&loop);
    if (runtime_active(runtime) || !loop.initialized || loop.active ||
        (loop.fault_flags != 0u) ||
        !alignment_controller_start(
            &runtime->alignment_controller,
            &runtime->motor_alignment,
            runtime->requested_alignment_current_counts,
            now_millis) ||
        !alignment_controller_get_reference_counts(
            &runtime->alignment_controller,
            &current_a_reference_counts,
            &current_b_reference_counts) ||
        !current_loop_backend_set_reference_counts(
            current_a_reference_counts,
            current_b_reference_counts) ||
        !current_loop_backend_start())
    {
        alignment_controller_abort(
            &runtime->alignment_controller, now_millis);
        (void)current_loop_backend_stop();
        board_bridge_force_low_zero();
        runtime->event_flags |= ROTOR_CONTROL_EVENT_FAULT;
        return false;
    }
    return true;
}

static bool start_torque(rotor_control_runtime_t* runtime,
                         const mt6816_sample_t* sample,
                         uint32_t now_millis,
                         uint32_t timestamp_us)
{
    current_loop_backend_snapshot_t loop = {0};
    uint32_t electrical_phase_q32 = 0u;

    current_loop_backend_get_snapshot(&loop);
    if (runtime_active(runtime) || !loop.initialized || loop.active ||
        (loop.fault_flags != 0u) ||
        !motor_alignment_electrical_phase_q32(
            &runtime->motor_alignment,
            sample->angle_raw,
            &electrical_phase_q32) ||
        !aligned_torque_controller_start(
            &runtime->torque_controller,
            runtime->requested_q_current_counts,
            runtime->requested_torque_duration_millis,
            now_millis,
            timestamp_us,
            float_to_q16_16(
                runtime->angle_tracker.velocity_revolutions_per_second)) ||
        !current_loop_backend_set_reference_counts(0, 0) ||
        !current_loop_backend_start())
    {
        if (aligned_torque_controller_is_active(
                &runtime->torque_controller))
        {
            (void)aligned_torque_controller_stop(
                &runtime->torque_controller, now_millis);
        }
        (void)current_loop_backend_stop();
        board_bridge_force_low_zero();
        runtime->event_flags |= ROTOR_CONTROL_EVENT_FAULT;
        return false;
    }
    return true;
}

static bool start_velocity(rotor_control_runtime_t* runtime,
                           const mt6816_sample_t* sample,
                           uint32_t now_millis,
                           uint32_t timestamp_us)
{
    current_loop_backend_snapshot_t loop = {0};
    uint32_t electrical_phase_q32 = 0u;
    const rotor_observation_t observation = runtime_observation(runtime);
    const int32_t velocity_q16_16 = float_to_q16_16(
        runtime->angle_tracker.velocity_revolutions_per_second);

    current_loop_backend_get_snapshot(&loop);
    if (runtime_active(runtime) || !loop.initialized || loop.active ||
        (loop.fault_flags != 0u) ||
        !motor_alignment_electrical_phase_q32(
            &runtime->motor_alignment,
            sample->angle_raw,
            &electrical_phase_q32) ||
        !velocity_controller_start(
            &runtime->velocity_controller,
            runtime->requested_velocity_revolutions_per_second_q16_16,
            runtime->requested_velocity_current_limit_counts,
            runtime->requested_velocity_duration_millis,
            runtime->motor_alignment.status.encoder_direction,
            now_millis,
            &observation) ||
        !aligned_torque_controller_start_tracking(
            &runtime->torque_controller,
            runtime->requested_velocity_duration_millis,
            now_millis,
            timestamp_us,
            velocity_q16_16) ||
        !current_loop_backend_set_reference_counts(0, 0) ||
        !current_loop_backend_start())
    {
        if (velocity_controller_is_active(&runtime->velocity_controller))
        {
            (void)velocity_controller_actuator_failed(
                &runtime->velocity_controller, now_millis);
        }
        if (aligned_torque_controller_is_active(
                &runtime->torque_controller))
        {
            (void)aligned_torque_controller_stop(
                &runtime->torque_controller, now_millis);
        }
        (void)current_loop_backend_stop();
        board_bridge_force_low_zero();
        runtime->event_flags |= ROTOR_CONTROL_EVENT_FAULT;
        return false;
    }
    return true;
}

static bool start_position(rotor_control_runtime_t* runtime,
                           const mt6816_sample_t* sample,
                           uint32_t now_millis,
                           uint32_t timestamp_us)
{
    current_loop_backend_snapshot_t loop = {0};
    uint32_t electrical_phase_q32 = 0u;
    const rotor_observation_t observation = runtime_observation(runtime);
    const int32_t velocity_q16_16 = float_to_q16_16(
        runtime->angle_tracker.velocity_revolutions_per_second);

    /*
     * Cascade deadline invariant: position, velocity, and torque start from
     * the same now/duration pair. update_position() evaluates the outer
     * position deadline first, so ordinary expiry releases every inner layer;
     * an earlier inner completion is therefore a contract fault.
     */

    current_loop_backend_get_snapshot(&loop);
    if (runtime_active(runtime) || !loop.initialized || loop.active ||
        (loop.fault_flags != 0u) ||
        !motor_alignment_electrical_phase_q32(
            &runtime->motor_alignment,
            sample->angle_raw,
            &electrical_phase_q32) ||
        !position_controller_start_relative(
            &runtime->position_controller,
            runtime->requested_position_displacement_revolutions_q16_16,
            runtime->requested_position_maximum_velocity_q16_16,
            runtime->requested_position_maximum_acceleration_q16_16,
            runtime->requested_position_current_limit_counts,
            runtime->requested_position_duration_millis,
            now_millis,
            &observation) ||
        !velocity_controller_start_tracking(
            &runtime->velocity_controller,
            0,
            runtime->requested_position_current_limit_counts,
            runtime->requested_position_duration_millis,
            runtime->motor_alignment.status.encoder_direction,
            now_millis,
            &observation) ||
        !aligned_torque_controller_start_tracking(
            &runtime->torque_controller,
            runtime->requested_position_duration_millis,
            now_millis,
            timestamp_us,
            velocity_q16_16) ||
        !current_loop_backend_set_reference_counts(0, 0) ||
        !current_loop_backend_start())
    {
        if (position_controller_is_active(&runtime->position_controller))
        {
            (void)position_controller_actuator_failed(
                &runtime->position_controller, now_millis);
        }
        if (velocity_controller_is_active(&runtime->velocity_controller))
        {
            (void)velocity_controller_actuator_failed(
                &runtime->velocity_controller, now_millis);
        }
        if (aligned_torque_controller_is_active(&runtime->torque_controller))
        {
            (void)aligned_torque_controller_stop(
                &runtime->torque_controller, now_millis);
        }
        (void)current_loop_backend_stop();
        board_bridge_force_low_zero();
        runtime->event_flags |= ROTOR_CONTROL_EVENT_FAULT;
        return false;
    }
    return true;
}

static void update_alignment(rotor_control_runtime_t* runtime,
                             const mt6816_sample_t* sample,
                             uint32_t now_millis)
{
    current_loop_backend_snapshot_t loop = {0};
    alignment_controller_event_t event;

    current_loop_backend_get_snapshot(&loop);
    event = alignment_controller_update(
        &runtime->alignment_controller,
        now_millis,
        true,
        sample->angle_raw,
        loop.latest_output.current_a_measured_counts,
        loop.latest_output.current_b_measured_counts,
        loop.active);
    if (event == ALIGNMENT_CONTROLLER_EVENT_REFERENCE_CHANGED)
    {
        int16_t current_a_reference_counts;
        int16_t current_b_reference_counts;

        if (!alignment_controller_get_reference_counts(
                &runtime->alignment_controller,
                &current_a_reference_counts,
                &current_b_reference_counts) ||
            !current_loop_backend_set_reference_counts(
                current_a_reference_counts,
                current_b_reference_counts))
        {
            alignment_controller_abort(
                &runtime->alignment_controller, now_millis);
            (void)current_loop_backend_stop();
            board_bridge_force_low_zero();
            runtime->event_flags |= ROTOR_CONTROL_EVENT_FAULT;
        }
    }
    else if (event == ALIGNMENT_CONTROLLER_EVENT_COMPLETED)
    {
        (void)stop_backend(
            runtime, ROTOR_CONTROL_EVENT_ALIGNMENT_COMPLETED);
    }
    else if (event == ALIGNMENT_CONTROLLER_EVENT_FAILED)
    {
        (void)stop_backend(
            runtime, ROTOR_CONTROL_EVENT_AUTHORITY_RELEASED);
    }
}

static void update_torque(rotor_control_runtime_t* runtime,
                          const mt6816_sample_t* sample,
                          uint32_t now_millis,
                          uint32_t timestamp_us)
{
    current_loop_backend_snapshot_t loop = {0};
    aligned_torque_status_t status;
    aligned_torque_event_t event;
    uint32_t electrical_phase_q32 = 0u;
    const bool phase_valid = motor_alignment_electrical_phase_q32(
        &runtime->motor_alignment,
        sample->angle_raw,
        &electrical_phase_q32);

    current_loop_backend_get_snapshot(&loop);
    event = aligned_torque_controller_update(
        &runtime->torque_controller,
        now_millis,
        timestamp_us,
        phase_valid,
        electrical_phase_q32,
        float_to_q16_16(
            runtime->angle_tracker.velocity_revolutions_per_second),
        loop.active);
    if (event == ALIGNED_TORQUE_EVENT_REFERENCE_CHANGED)
    {
        aligned_torque_controller_get_status(
            &runtime->torque_controller, &status);
        if (!publish_aligned_q_reference(
                runtime, &status, timestamp_us))
        {
            (void)aligned_torque_controller_reference_rejected(
                &runtime->torque_controller, now_millis);
            event = ALIGNED_TORQUE_EVENT_FAILED;
        }
    }
    if (event == ALIGNED_TORQUE_EVENT_COMPLETED)
    {
        (void)stop_backend(
            runtime, ROTOR_CONTROL_EVENT_AUTHORITY_RELEASED);
    }
    else if (event == ALIGNED_TORQUE_EVENT_FAILED)
    {
        (void)current_loop_backend_stop();
        board_bridge_force_low_zero();
        runtime->event_flags |= ROTOR_CONTROL_EVENT_FAULT;
    }
}

static void update_velocity(rotor_control_runtime_t* runtime,
                            const mt6816_sample_t* sample,
                            uint32_t now_millis,
                            uint32_t timestamp_us)
{
    current_loop_backend_snapshot_t loop = {0};
    aligned_torque_status_t torque_status;
    velocity_control_event_t velocity_event;
    aligned_torque_event_t torque_event = ALIGNED_TORQUE_EVENT_NONE;
    rotor_observation_t observation = runtime_observation(runtime);
    uint32_t electrical_phase_q32 = 0u;
    int16_t q_current_request = 0;
    const bool phase_valid = motor_alignment_electrical_phase_q32(
        &runtime->motor_alignment,
        sample->angle_raw,
        &electrical_phase_q32);

    observation.timestamp_us = timestamp_us;
    current_loop_backend_get_snapshot(&loop);
    velocity_event = velocity_controller_update(
        &runtime->velocity_controller,
        now_millis,
        &observation,
        &q_current_request);
    if (velocity_event == VELOCITY_CONTROL_EVENT_CURRENT_CHANGED)
    {
        if (!aligned_torque_controller_set_target(
                &runtime->torque_controller, q_current_request))
        {
            (void)velocity_controller_actuator_failed(
                &runtime->velocity_controller, now_millis);
            velocity_event = VELOCITY_CONTROL_EVENT_FAILED;
        }
        else
        {
            torque_event = aligned_torque_controller_update(
                &runtime->torque_controller,
                now_millis,
                timestamp_us,
                phase_valid,
                electrical_phase_q32,
                float_to_q16_16(
                    observation.velocity_revolutions_per_second),
                loop.active);
            if (torque_event == ALIGNED_TORQUE_EVENT_REFERENCE_CHANGED)
            {
                aligned_torque_controller_get_status(
                    &runtime->torque_controller, &torque_status);
                if (!publish_aligned_q_reference(
                        runtime, &torque_status, timestamp_us))
                {
                    (void)aligned_torque_controller_reference_rejected(
                        &runtime->torque_controller, now_millis);
                    torque_event = ALIGNED_TORQUE_EVENT_FAILED;
                }
            }
            if (torque_event != ALIGNED_TORQUE_EVENT_REFERENCE_CHANGED)
            {
                (void)velocity_controller_actuator_failed(
                    &runtime->velocity_controller, now_millis);
                velocity_event = VELOCITY_CONTROL_EVENT_FAILED;
            }
        }
    }

    if (velocity_event == VELOCITY_CONTROL_EVENT_COMPLETED)
    {
        if (aligned_torque_controller_is_active(&runtime->torque_controller))
        {
            (void)aligned_torque_controller_stop(
                &runtime->torque_controller, now_millis);
        }
        (void)stop_backend(
            runtime, ROTOR_CONTROL_EVENT_AUTHORITY_RELEASED);
    }
    else if (velocity_event == VELOCITY_CONTROL_EVENT_FAILED)
    {
        if (aligned_torque_controller_is_active(&runtime->torque_controller))
        {
            (void)aligned_torque_controller_stop(
                &runtime->torque_controller, now_millis);
        }
        (void)current_loop_backend_stop();
        board_bridge_force_low_zero();
        runtime->event_flags |= ROTOR_CONTROL_EVENT_FAULT;
    }
}

static void update_position(rotor_control_runtime_t* runtime,
                            const mt6816_sample_t* sample,
                            uint32_t now_millis,
                            uint32_t timestamp_us)
{
    current_loop_backend_snapshot_t loop = {0};
    aligned_torque_status_t torque_status;
    position_control_event_t position_event;
    velocity_control_event_t velocity_event =
        VELOCITY_CONTROL_EVENT_NONE;
    aligned_torque_event_t torque_event = ALIGNED_TORQUE_EVENT_NONE;
    rotor_observation_t observation = runtime_observation(runtime);
    uint32_t electrical_phase_q32 = 0u;
    int32_t velocity_target_q16_16 = 0;
    int16_t q_current_request = 0;
    const bool phase_valid = motor_alignment_electrical_phase_q32(
        &runtime->motor_alignment,
        sample->angle_raw,
        &electrical_phase_q32);

    observation.timestamp_us = timestamp_us;
    current_loop_backend_get_snapshot(&loop);
    position_event = position_controller_update(
        &runtime->position_controller,
        now_millis,
        &observation,
        &velocity_target_q16_16);
    if (position_event == POSITION_CONTROL_EVENT_VELOCITY_CHANGED)
    {
        if (!velocity_controller_set_target(
                &runtime->velocity_controller, velocity_target_q16_16))
        {
            (void)position_controller_actuator_failed(
                &runtime->position_controller, now_millis);
            position_event = POSITION_CONTROL_EVENT_FAILED;
        }
        else
        {
            velocity_event = velocity_controller_update(
                &runtime->velocity_controller,
                now_millis,
                &observation,
                &q_current_request);
            if ((velocity_event ==
                 VELOCITY_CONTROL_EVENT_CURRENT_CHANGED) &&
                aligned_torque_controller_set_target(
                    &runtime->torque_controller, q_current_request))
            {
                torque_event = aligned_torque_controller_update(
                    &runtime->torque_controller,
                    now_millis,
                    timestamp_us,
                    phase_valid,
                    electrical_phase_q32,
                    float_to_q16_16(
                        observation.velocity_revolutions_per_second),
                    loop.active);
                if (torque_event == ALIGNED_TORQUE_EVENT_REFERENCE_CHANGED)
                {
                    aligned_torque_controller_get_status(
                        &runtime->torque_controller, &torque_status);
                    if (!publish_aligned_q_reference(
                            runtime, &torque_status, timestamp_us))
                    {
                        (void)aligned_torque_controller_reference_rejected(
                            &runtime->torque_controller, now_millis);
                        torque_event = ALIGNED_TORQUE_EVENT_FAILED;
                    }
                }
            }
            if ((velocity_event !=
                 VELOCITY_CONTROL_EVENT_CURRENT_CHANGED) ||
                (torque_event != ALIGNED_TORQUE_EVENT_REFERENCE_CHANGED))
            {
                (void)position_controller_actuator_failed(
                    &runtime->position_controller, now_millis);
                position_event = POSITION_CONTROL_EVENT_FAILED;
            }
        }
    }

    if (position_event == POSITION_CONTROL_EVENT_COMPLETED)
    {
        if (velocity_controller_is_active(&runtime->velocity_controller))
        {
            (void)velocity_controller_stop(
                &runtime->velocity_controller, now_millis);
        }
        if (aligned_torque_controller_is_active(&runtime->torque_controller))
        {
            (void)aligned_torque_controller_stop(
                &runtime->torque_controller, now_millis);
        }
        (void)stop_backend(
            runtime, ROTOR_CONTROL_EVENT_AUTHORITY_RELEASED);
    }
    else if (position_event == POSITION_CONTROL_EVENT_FAILED)
    {
        if (velocity_controller_is_active(&runtime->velocity_controller))
        {
            (void)velocity_controller_actuator_failed(
                &runtime->velocity_controller, now_millis);
        }
        if (aligned_torque_controller_is_active(&runtime->torque_controller))
        {
            (void)aligned_torque_controller_stop(
                &runtime->torque_controller, now_millis);
        }
        (void)current_loop_backend_stop();
        board_bridge_force_low_zero();
        runtime->event_flags |= ROTOR_CONTROL_EVENT_FAULT;
    }
}

bool rotor_control_runtime_init(
    rotor_control_runtime_t* runtime,
    const angle_tracker_t* angle_tracker,
    const motor_alignment_t* motor_alignment,
    const alignment_controller_t* alignment_controller,
    const aligned_torque_controller_t* torque_controller,
    const velocity_controller_t* velocity_controller,
    const position_controller_t* position_controller)
{
    const diagnostics_encoder_t empty_encoder = {
        .status = MT6816_STATUS_NOT_ATTEMPTED,
        .transport_status = SPI_STATUS_NOT_READY,
    };

    if ((runtime == NULL) || (angle_tracker == NULL) ||
        (motor_alignment == NULL) || (alignment_controller == NULL) ||
        (torque_controller == NULL) || (velocity_controller == NULL) ||
        (position_controller == NULL) ||
        !angle_tracker_config_is_valid(&angle_tracker->config) ||
        !motor_alignment->initialized ||
        !alignment_controller->initialized ||
        !torque_controller->initialized || !velocity_controller->initialized ||
        !position_controller->initialized)
    {
        return false;
    }
    runtime->angle_tracker = *angle_tracker;
    runtime->motor_alignment = *motor_alignment;
    runtime->alignment_controller = *alignment_controller;
    runtime->alignment_controller.alignment = NULL;
    runtime->torque_controller = *torque_controller;
    runtime->velocity_controller = *velocity_controller;
    runtime->position_controller = *position_controller;
    runtime->encoder_diagnostics = empty_encoder;
    runtime->estimator_fault_flags = 0u;
    runtime->estimator_sample_interval_us = 0u;
    runtime->estimator_maximum_sample_interval_us = 0u;
    runtime->progress_sequence = 0u;
    runtime->snapshot_sequence = 0u;
    runtime->last_full_snapshot_timestamp_us = 0u;
    runtime->full_snapshot_timestamp_valid = false;
    runtime->request_flags = 0u;
    runtime->requested_alignment_current_counts = 0u;
    runtime->requested_q_current_counts = 0;
    runtime->requested_torque_duration_millis = 0u;
    runtime->requested_velocity_revolutions_per_second_q16_16 = 0;
    runtime->requested_velocity_current_limit_counts = 0u;
    runtime->requested_velocity_duration_millis = 0u;
    runtime->requested_position_displacement_revolutions_q16_16 = 0;
    runtime->requested_position_maximum_velocity_q16_16 = 0;
    runtime->requested_position_maximum_acceleration_q16_16 = 0;
    runtime->requested_position_current_limit_counts = 0u;
    runtime->requested_position_duration_millis = 0u;
    runtime->event_flags = 0u;
    runtime->initialized = true;
    publish_full_snapshot(runtime);
    publish_progress_snapshot(runtime);
    return true;
}

bool rotor_control_runtime_request_alignment(
    rotor_control_runtime_t* runtime,
    uint16_t alignment_current_counts)
{
    uint32_t previous;

    if ((runtime == NULL) || !runtime->initialized ||
        (alignment_current_counts == 0u))
    {
        return false;
    }
    previous = runtime_critical_enter();
    if ((runtime->request_flags != 0u) || runtime_active(runtime))
    {
        runtime_critical_exit(previous);
        return false;
    }
    runtime->requested_alignment_current_counts =
        alignment_current_counts;
    __DMB();
    runtime->request_flags = ROTOR_CONTROL_REQUEST_ALIGNMENT;
    runtime_critical_exit(previous);
    return true;
}

bool rotor_control_runtime_request_torque(
    rotor_control_runtime_t* runtime,
    int16_t q_current_counts,
    uint32_t duration_millis)
{
    uint32_t previous;

    if ((runtime == NULL) || !runtime->initialized ||
        (q_current_counts == 0) || (duration_millis == 0u))
    {
        return false;
    }
    previous = runtime_critical_enter();
    if ((runtime->request_flags != 0u) || runtime_active(runtime))
    {
        runtime_critical_exit(previous);
        return false;
    }
    runtime->requested_q_current_counts = q_current_counts;
    runtime->requested_torque_duration_millis = duration_millis;
    __DMB();
    runtime->request_flags = ROTOR_CONTROL_REQUEST_TORQUE;
    runtime_critical_exit(previous);
    return true;
}

bool rotor_control_runtime_request_velocity(
    rotor_control_runtime_t* runtime,
    int32_t velocity_revolutions_per_second_q16_16,
    uint16_t current_limit_counts,
    uint32_t duration_millis)
{
    uint32_t previous;

    if ((runtime == NULL) || !runtime->initialized ||
        (velocity_revolutions_per_second_q16_16 == 0) ||
        (current_limit_counts == 0u) || (duration_millis == 0u))
    {
        return false;
    }
    previous = runtime_critical_enter();
    if ((runtime->request_flags != 0u) || runtime_active(runtime))
    {
        runtime_critical_exit(previous);
        return false;
    }
    runtime->requested_velocity_revolutions_per_second_q16_16 =
        velocity_revolutions_per_second_q16_16;
    runtime->requested_velocity_current_limit_counts = current_limit_counts;
    runtime->requested_velocity_duration_millis = duration_millis;
    __DMB();
    runtime->request_flags = ROTOR_CONTROL_REQUEST_VELOCITY;
    runtime_critical_exit(previous);
    return true;
}

bool rotor_control_runtime_request_position_relative(
    rotor_control_runtime_t* runtime,
    int32_t displacement_revolutions_q16_16,
    int32_t maximum_velocity_revolutions_per_second_q16_16,
    int32_t maximum_acceleration_revolutions_per_second2_q16_16,
    uint16_t current_limit_counts,
    uint32_t duration_millis)
{
    uint32_t previous;

    if ((runtime == NULL) || !runtime->initialized ||
        (displacement_revolutions_q16_16 == 0) ||
        (maximum_velocity_revolutions_per_second_q16_16 <= 0) ||
        (maximum_acceleration_revolutions_per_second2_q16_16 <= 0) ||
        (current_limit_counts == 0u) || (duration_millis == 0u))
    {
        return false;
    }
    previous = runtime_critical_enter();
    if ((runtime->request_flags != 0u) || runtime_active(runtime))
    {
        runtime_critical_exit(previous);
        return false;
    }
    runtime->requested_position_displacement_revolutions_q16_16 =
        displacement_revolutions_q16_16;
    runtime->requested_position_maximum_velocity_q16_16 =
        maximum_velocity_revolutions_per_second_q16_16;
    runtime->requested_position_maximum_acceleration_q16_16 =
        maximum_acceleration_revolutions_per_second2_q16_16;
    runtime->requested_position_current_limit_counts = current_limit_counts;
    runtime->requested_position_duration_millis = duration_millis;
    __DMB();
    runtime->request_flags = ROTOR_CONTROL_REQUEST_POSITION;
    runtime_critical_exit(previous);
    return true;
}

void rotor_control_runtime_request_stop(rotor_control_runtime_t* runtime)
{
    uint32_t previous;

    if ((runtime == NULL) || !runtime->initialized)
    {
        return;
    }
    previous = runtime_critical_enter();
    runtime->request_flags = ROTOR_CONTROL_REQUEST_STOP;
    runtime_critical_exit(previous);
}

void rotor_control_runtime_force_fault(rotor_control_runtime_t* runtime,
                                       uint32_t timestamp_us)
{
    uint32_t previous;

    if ((runtime == NULL) || !runtime->initialized)
    {
        board_bridge_force_low_zero();
        return;
    }
    previous = runtime_critical_enter();
    runtime->request_flags = 0u;
    if (runtime_active(runtime))
    {
        fail_active_control(
            runtime, timebase_millis(), timestamp_us);
    }
    else
    {
        (void)current_loop_backend_stop();
        board_bridge_force_low_zero();
        runtime->event_flags |= ROTOR_CONTROL_EVENT_FAULT;
    }
    publish_full_snapshot(runtime);
    publish_progress_snapshot(runtime);
    runtime_critical_exit(previous);
}

bool rotor_control_runtime_clear_faults(
    rotor_control_runtime_t* runtime,
    uint32_t* cleared_fault_sources)
{
    angle_tracker_t angle_tracker;
    alignment_controller_t alignment_controller;
    aligned_torque_controller_t torque_controller;
    velocity_controller_t velocity_controller;
    position_controller_t position_controller;
    uint32_t sources = 0u;
    uint32_t previous;
    bool reset_estimator;

    if (cleared_fault_sources != NULL)
    {
        *cleared_fault_sources = 0u;
    }
    if ((runtime == NULL) || !runtime->initialized)
    {
        return false;
    }

    previous = runtime_critical_enter();
    reset_estimator = runtime->estimator_fault_flags != 0u;
    if (reset_estimator)
    {
        sources |= ROTOR_CONTROL_FAULT_SOURCE_ESTIMATOR;
    }
    if (runtime->alignment_controller.status.state ==
        ALIGNMENT_CONTROLLER_STATE_FAILED)
    {
        sources |= ROTOR_CONTROL_FAULT_SOURCE_ALIGNMENT;
    }
    if ((runtime->torque_controller.status.state ==
         ALIGNED_TORQUE_STATE_FAILED) ||
        (runtime->torque_controller.status.fault_flags != 0u))
    {
        sources |= ROTOR_CONTROL_FAULT_SOURCE_ALIGNED_TORQUE;
    }
    if ((runtime->velocity_controller.status.state ==
         VELOCITY_CONTROL_STATE_FAILED) ||
        (runtime->velocity_controller.status.fault_flags != 0u))
    {
        sources |= ROTOR_CONTROL_FAULT_SOURCE_VELOCITY;
    }
    if ((runtime->position_controller.status.state ==
         POSITION_CONTROL_STATE_FAILED) ||
        (runtime->position_controller.status.fault_flags != 0u))
    {
        sources |= ROTOR_CONTROL_FAULT_SOURCE_POSITION;
    }

    angle_tracker = runtime->angle_tracker;
    if ((reset_estimator &&
         !angle_tracker_init(&angle_tracker,
                             &runtime->angle_tracker.config)) ||
        !alignment_controller_init(
            &alignment_controller,
            &runtime->alignment_controller.config) ||
        !aligned_torque_controller_init(
            &torque_controller,
            &runtime->torque_controller.config) ||
        !velocity_controller_init(
            &velocity_controller,
            &runtime->velocity_controller.config) ||
        !position_controller_init(
            &position_controller,
            &runtime->position_controller.config))
    {
        runtime_critical_exit(previous);
        return false;
    }

    if (reset_estimator)
    {
        runtime->angle_tracker = angle_tracker;
    }
    runtime->alignment_controller = alignment_controller;
    runtime->alignment_controller.alignment = NULL;
    runtime->torque_controller = torque_controller;
    runtime->velocity_controller = velocity_controller;
    runtime->position_controller = position_controller;
    runtime->estimator_fault_flags = 0u;
    runtime->estimator_sample_interval_us = 0u;
    runtime->estimator_maximum_sample_interval_us = 0u;
    runtime->request_flags = 0u;
    runtime->requested_alignment_current_counts = 0u;
    runtime->requested_q_current_counts = 0;
    runtime->requested_torque_duration_millis = 0u;
    runtime->requested_velocity_revolutions_per_second_q16_16 = 0;
    runtime->requested_velocity_current_limit_counts = 0u;
    runtime->requested_velocity_duration_millis = 0u;
    runtime->requested_position_displacement_revolutions_q16_16 = 0;
    runtime->requested_position_maximum_velocity_q16_16 = 0;
    runtime->requested_position_maximum_acceleration_q16_16 = 0;
    runtime->requested_position_current_limit_counts = 0u;
    runtime->requested_position_duration_millis = 0u;
    runtime->event_flags = ROTOR_CONTROL_EVENT_NONE;
    publish_full_snapshot(runtime);
    publish_progress_snapshot(runtime);
    runtime_critical_exit(previous);

    if (cleared_fault_sources != NULL)
    {
        *cleared_fault_sources = sources;
    }
    return true;
}

bool rotor_control_runtime_clear_alignment(
    rotor_control_runtime_t* runtime)
{
    uint32_t previous;

    if ((runtime == NULL) || !runtime->initialized)
    {
        return false;
    }
    previous = runtime_critical_enter();
    if (runtime_active(runtime) || (runtime->request_flags != 0u))
    {
        runtime_critical_exit(previous);
        return false;
    }
    motor_alignment_clear(&runtime->motor_alignment);
    publish_full_snapshot(runtime);
    publish_progress_snapshot(runtime);
    runtime_critical_exit(previous);
    return true;
}

uint32_t rotor_control_runtime_take_events(
    rotor_control_runtime_t* runtime)
{
    uint32_t previous;
    uint32_t events;

    if ((runtime == NULL) || !runtime->initialized)
    {
        return ROTOR_CONTROL_EVENT_NONE;
    }
    previous = runtime_critical_enter();
    events = runtime->event_flags;
    runtime->event_flags = ROTOR_CONTROL_EVENT_NONE;
    runtime_critical_exit(previous);
    return events;
}

bool rotor_control_runtime_get_snapshot(
    const rotor_control_runtime_t* runtime,
    rotor_control_snapshot_t* snapshot)
{
    uint32_t before;
    uint32_t after = 0u;

    if ((runtime == NULL) || (snapshot == NULL) ||
        !runtime->initialized)
    {
        return false;
    }
    do
    {
        before = runtime->snapshot_sequence;
        if ((before & 1u) != 0u)
        {
            continue;
        }
        __DMB();
        *snapshot = runtime->published;
        __DMB();
        after = runtime->snapshot_sequence;
    } while ((before != after) || ((after & 1u) != 0u));
    return true;
}

bool rotor_control_runtime_get_progress_snapshot(
    const rotor_control_runtime_t* runtime,
    rotor_control_progress_snapshot_t* snapshot)
{
    uint32_t before;
    uint32_t after = 0u;

    if ((runtime == NULL) || (snapshot == NULL) ||
        !runtime->initialized)
    {
        return false;
    }
    do
    {
        before = runtime->progress_sequence;
        if ((before & 1u) != 0u)
        {
            continue;
        }
        __DMB();
        *snapshot = runtime->progress_published;
        __DMB();
        after = runtime->progress_sequence;
    } while ((before != after) || ((after & 1u) != 0u));
    return true;
}

void rotor_control_runtime_spi_callback(
    void* context,
    spi_status_t transport_status,
    const uint8_t* receive,
    size_t length,
    uint32_t timestamp_us)
{
    rotor_control_runtime_t* runtime = context;
    mt6816_sample_t sample = {0};
    mt6816_status_t encoder_status;
    uint32_t requests;
    uint32_t events_before;
    uint32_t now_millis;
    bool started = false;

    if ((runtime == NULL) || !runtime->initialized)
    {
        board_bridge_force_low_zero();
        return;
    }
    events_before = runtime->event_flags;
    now_millis = timebase_millis();
    runtime->encoder_diagnostics.last_attempt_millis = now_millis;
    runtime->encoder_diagnostics.transport_status = transport_status;
    if ((transport_status != SPI_STATUS_OK) || (receive == NULL) ||
        (length != ROTOR_CONTROL_MT6816_RESPONSE_LENGTH))
    {
        encoder_status = MT6816_STATUS_TRANSPORT_ERROR;
    }
    else
    {
        encoder_status = mt6816_decode_registers(
            receive[1], receive[2], receive[3], &sample);
    }
    runtime->encoder_diagnostics.status = encoder_status;
    if (encoder_status != MT6816_STATUS_OK)
    {
        ++runtime->encoder_diagnostics.error_count;
        reject_requests_without_feedback(
            runtime, now_millis, timestamp_us);
        publish_callback_state(
            runtime,
            timestamp_us,
            runtime->event_flags != events_before);
        return;
    }

    runtime->encoder_diagnostics.angle_raw = sample.angle_raw;
    runtime->encoder_diagnostics.flags = sample.flags;
    ++runtime->encoder_diagnostics.sample_count;
    if (runtime->angle_tracker.initialized)
    {
        const uint32_t previous_timestamp_us =
            runtime->angle_tracker.last_timestamp_us;

        if (previous_timestamp_us != 0u)
        {
            runtime->estimator_sample_interval_us =
                timestamp_us - previous_timestamp_us;
            if (runtime->estimator_sample_interval_us >
                runtime->estimator_maximum_sample_interval_us)
            {
                runtime->estimator_maximum_sample_interval_us =
                    runtime->estimator_sample_interval_us;
            }
        }
    }
    if ((sample.flags != 0u) ||
        !angle_tracker_push(
            &runtime->angle_tracker, sample.angle_raw, timestamp_us))
    {
        runtime->estimator_fault_flags |=
            ROTOR_CONTROL_ESTIMATOR_FAULT_INVALID_SAMPLE;
        reject_requests_without_feedback(
            runtime, now_millis, timestamp_us);
        publish_callback_state(
            runtime,
            timestamp_us,
            runtime->event_flags != events_before);
        return;
    }

    requests = runtime->request_flags;
    runtime->request_flags = 0u;
    if ((requests & ROTOR_CONTROL_REQUEST_STOP) != 0u)
    {
        process_stop_request(runtime, now_millis);
        publish_callback_state(runtime, timestamp_us, true);
        return;
    }
    if ((requests & ROTOR_CONTROL_REQUEST_ALIGNMENT) != 0u)
    {
        started = start_alignment(runtime, now_millis);
    }
    else if ((requests & ROTOR_CONTROL_REQUEST_TORQUE) != 0u)
    {
        started = start_torque(
            runtime, &sample, now_millis, timestamp_us);
    }
    else if ((requests & ROTOR_CONTROL_REQUEST_VELOCITY) != 0u)
    {
        started = start_velocity(
            runtime, &sample, now_millis, timestamp_us);
    }
    else if ((requests & ROTOR_CONTROL_REQUEST_POSITION) != 0u)
    {
        started = start_position(
            runtime, &sample, now_millis, timestamp_us);
    }

    if (!started)
    {
        if (alignment_controller_is_active(&runtime->alignment_controller))
        {
            update_alignment(runtime, &sample, now_millis);
        }
        else if (position_controller_is_active(
                     &runtime->position_controller))
        {
            update_position(runtime, &sample, now_millis, timestamp_us);
        }
        else if (velocity_controller_is_active(
                     &runtime->velocity_controller))
        {
            update_velocity(runtime, &sample, now_millis, timestamp_us);
        }
        else if (aligned_torque_controller_is_active(
                     &runtime->torque_controller))
        {
            update_torque(runtime, &sample, now_millis, timestamp_us);
        }
    }
    publish_callback_state(
        runtime,
        timestamp_us,
        (requests != 0u) || (runtime->event_flags != events_before));
}
