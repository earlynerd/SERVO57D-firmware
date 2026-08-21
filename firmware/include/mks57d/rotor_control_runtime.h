#ifndef MKS57D_ROTOR_CONTROL_RUNTIME_H
#define MKS57D_ROTOR_CONTROL_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mks57d/aligned_torque_controller.h"
#include "mks57d/alignment_controller.h"
#include "mks57d/angle_tracker.h"
#include "mks57d/diagnostics.h"
#include "mks57d/motor_alignment.h"
#include "mks57d/rotor_observation.h"
#include "mks57d/spi_bus.h"

enum
{
    ROTOR_CONTROL_EVENT_NONE = 0u,
    ROTOR_CONTROL_EVENT_AUTHORITY_RELEASED = 1u << 0,
    ROTOR_CONTROL_EVENT_ALIGNMENT_COMPLETED = 1u << 1,
    ROTOR_CONTROL_EVENT_FAULT = 1u << 2
};

typedef struct
{
    diagnostics_encoder_t encoder_diagnostics;
    rotor_observation_t observation;
    angle_tracker_t angle_tracker;
    motor_alignment_t motor_alignment;
    alignment_controller_t alignment_controller;
    aligned_torque_controller_t torque_controller;
    uint32_t estimator_fault_flags;
    uint32_t estimator_sample_interval_us;
    uint32_t estimator_maximum_sample_interval_us;
} rotor_control_snapshot_t;

typedef struct
{
    angle_tracker_t angle_tracker;
    motor_alignment_t motor_alignment;
    alignment_controller_t alignment_controller;
    aligned_torque_controller_t torque_controller;
    diagnostics_encoder_t encoder_diagnostics;
    uint32_t estimator_fault_flags;
    uint32_t estimator_sample_interval_us;
    uint32_t estimator_maximum_sample_interval_us;
    volatile uint32_t snapshot_sequence;
    rotor_control_snapshot_t published;
    volatile uint32_t request_flags;
    uint16_t requested_alignment_current_counts;
    int16_t requested_q_current_counts;
    uint32_t requested_torque_duration_millis;
    volatile uint32_t event_flags;
    bool initialized;
} rotor_control_runtime_t;

bool rotor_control_runtime_init(
    rotor_control_runtime_t* runtime,
    const angle_tracker_t* angle_tracker,
    const motor_alignment_t* motor_alignment,
    const alignment_controller_t* alignment_controller,
    const aligned_torque_controller_t* torque_controller);
bool rotor_control_runtime_request_alignment(
    rotor_control_runtime_t* runtime,
    uint16_t alignment_current_counts);
bool rotor_control_runtime_request_torque(
    rotor_control_runtime_t* runtime,
    int16_t q_current_counts,
    uint32_t duration_millis);
void rotor_control_runtime_request_stop(rotor_control_runtime_t* runtime);
void rotor_control_runtime_force_fault(rotor_control_runtime_t* runtime,
                                       uint32_t timestamp_us);
bool rotor_control_runtime_clear_alignment(
    rotor_control_runtime_t* runtime);
uint32_t rotor_control_runtime_take_events(
    rotor_control_runtime_t* runtime);
bool rotor_control_runtime_get_snapshot(
    const rotor_control_runtime_t* runtime,
    rotor_control_snapshot_t* snapshot);
void rotor_control_runtime_spi_callback(
    void* context,
    spi_status_t transport_status,
    const uint8_t* receive,
    size_t length,
    uint32_t timestamp_us);

#endif
