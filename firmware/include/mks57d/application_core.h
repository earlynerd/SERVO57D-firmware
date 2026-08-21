#ifndef MKS57D_APPLICATION_CORE_H
#define MKS57D_APPLICATION_CORE_H

#include <stdbool.h>
#include <stdint.h>

#include "mks57d/motion_manager.h"
#include "mks57d/servo_core.h"
#include "mks57d/step_direction.h"

typedef enum
{
    APPLICATION_CORE_STATUS_OK = 0,
    APPLICATION_CORE_STATUS_NOT_READY,
    APPLICATION_CORE_STATUS_INVALID_ARGUMENT,
    APPLICATION_CORE_STATUS_FAULTED
} application_core_status_t;

typedef struct
{
    servo_core_config_t servo;
    motion_manager_config_t motion;
    step_direction_config_t step_direction;
} application_core_config_t;

typedef struct
{
    motion_manager_status_t motion;
    servo_core_output_t servo;
    bool control_enabled;
    float torque_current_request_amperes;
} application_core_output_t;

typedef struct
{
    application_core_config_t config;
    servo_core_t servo;
    motion_manager_t motion;
    step_direction_t step_direction;
    servo_core_output_t last_servo_output;
    uint32_t step_direction_command_id;
    bool motion_ready;
    bool control_enabled;
    bool initialized;
} application_core_t;

bool application_core_config_is_valid(
    const application_core_config_t* config);
bool application_core_init(application_core_t* application,
                           const application_core_config_t* config);
application_core_status_t application_core_observe_rotor(
    application_core_t* application,
    const rotor_observation_t* observation);
motion_submit_status_t application_core_submit_motion(
    application_core_t* application,
    const motion_request_t* request,
    uint32_t timestamp_us);
bool application_core_update_step_direction(
    application_core_t* application,
    int32_t cumulative_steps,
    bool enabled,
    uint32_t timestamp_us,
    motion_submit_status_t* submit_status);
application_core_status_t application_core_step(
    application_core_t* application,
    uint32_t timestamp_us,
    application_core_output_t* output);
bool application_core_recover(application_core_t* application,
                              bool safe_to_recover,
                              const rotor_observation_t* observation);

#endif
