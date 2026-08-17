#ifndef MKS57D_SERVO_CORE_H
#define MKS57D_SERVO_CORE_H

#include <stdbool.h>
#include <stdint.h>

#include "mks57d/angle_tracker.h"
#include "mks57d/motion_profile.h"
#include "mks57d/pi_controller.h"

typedef enum
{
    SERVO_FAULT_NONE = 0u,
    SERVO_FAULT_INVALID_CONFIGURATION = 1u << 0,
    SERVO_FAULT_INVALID_ENCODER_SAMPLE = 1u << 1,
    SERVO_FAULT_STALE_ENCODER = 1u << 2,
    SERVO_FAULT_CONTROL_DEADLINE = 1u << 3,
    SERVO_FAULT_FOLLOWING_ERROR = 1u << 4,
    SERVO_FAULT_INTERNAL_NUMERIC = 1u << 5
} servo_fault_t;

typedef enum
{
    SERVO_CORE_STATUS_OK = 0,
    SERVO_CORE_STATUS_NOT_READY,
    SERVO_CORE_STATUS_INVALID_ARGUMENT,
    SERVO_CORE_STATUS_FAULTED
} servo_core_status_t;

typedef struct
{
    angle_tracker_config_t angle_tracker;
    motion_profile_config_t motion_profile;
    pi_controller_config_t velocity_controller;
    uint32_t encoder_stale_timeout_us;
    uint32_t maximum_control_interval_us;
    float position_gain_per_second;
    float maximum_following_error_revolutions;
    float maximum_current_amperes;
} servo_core_config_t;

typedef struct
{
    bool valid;
    bool trajectory_settled;
    uint32_t fault_flags;
    float measured_position_revolutions;
    float measured_velocity_revolutions_per_second;
    float reference_position_revolutions;
    float reference_velocity_revolutions_per_second;
    float velocity_request_revolutions_per_second;
    float torque_current_request_amperes;
} servo_core_output_t;

typedef struct
{
    servo_core_config_t config;
    angle_tracker_t angle_tracker;
    motion_profile_t motion_profile;
    pi_controller_t velocity_controller;
    uint32_t last_control_timestamp_us;
    uint32_t fault_flags;
    bool initialized;
    bool feedback_ready;
    bool suspended;
} servo_core_t;

bool servo_core_config_is_valid(const servo_core_config_t* config);
bool servo_core_init(servo_core_t* core,
                     const servo_core_config_t* config);
servo_core_status_t servo_core_observe_encoder(servo_core_t* core,
                                                uint16_t raw_angle,
                                                uint32_t timestamp_us);
servo_core_status_t servo_core_set_position_target(
    servo_core_t* core,
    float target_position_revolutions);
servo_core_status_t servo_core_request_stop(servo_core_t* core);
servo_core_status_t servo_core_suspend(servo_core_t* core);
servo_core_status_t servo_core_resume(servo_core_t* core,
                                      uint32_t timestamp_us);
servo_core_status_t servo_core_step(servo_core_t* core,
                                    uint32_t timestamp_us,
                                    servo_core_output_t* output);
bool servo_core_is_faulted(const servo_core_t* core);

#endif
