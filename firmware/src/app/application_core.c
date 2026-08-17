#include "mks57d/application_core.h"

#include <math.h>
#include <stddef.h>

static uint32_t next_step_direction_command_id(application_core_t* application)
{
    ++application->step_direction_command_id;
    if (application->step_direction_command_id == 0u)
    {
        ++application->step_direction_command_id;
    }
    return application->step_direction_command_id;
}

static bool apply_action(application_core_t* application,
                         const motion_action_t* action,
                         uint32_t timestamp_us)
{
    switch (action->kind)
    {
        case MOTION_ACTION_NONE:
            return true;

        case MOTION_ACTION_ENABLE:
            if (servo_core_resume(&application->servo, timestamp_us) !=
                SERVO_CORE_STATUS_OK)
            {
                return false;
            }
            application->control_enabled = true;
            return true;

        case MOTION_ACTION_SET_POSITION_TARGET:
            return servo_core_set_position_target(
                       &application->servo,
                       action->position_revolutions) ==
                   SERVO_CORE_STATUS_OK;

        case MOTION_ACTION_REQUEST_CONTROLLED_STOP:
            if (servo_core_request_stop(&application->servo) !=
                SERVO_CORE_STATUS_OK)
            {
                return false;
            }
            application->motion.target_position_revolutions =
                application->servo.motion_profile.target_position_revolutions;
            return true;

        case MOTION_ACTION_DISABLE:
            application->control_enabled = false;
            if (servo_core_is_faulted(&application->servo))
            {
                return true;
            }
            return servo_core_suspend(&application->servo) ==
                   SERVO_CORE_STATUS_OK;

        default:
            return false;
    }
}

static void publish_output(application_core_t* application,
                           application_core_output_t* output)
{
    motion_manager_get_status(&application->motion, &output->motion);
    output->servo = application->last_servo_output;
    output->control_enabled = application->control_enabled;
    output->torque_current_request_amperes =
        (application->control_enabled && output->servo.valid) ?
            output->servo.torque_current_request_amperes : 0.0f;
}

static bool latch_application_fault(application_core_t* application,
                                    uint32_t timestamp_us)
{
    motion_action_t action;

    if (!motion_manager_poll(&application->motion,
                             timestamp_us,
                             false,
                             true,
                             &action))
    {
        return false;
    }
    application->control_enabled = false;
    return apply_action(application, &action, timestamp_us);
}

bool application_core_config_is_valid(
    const application_core_config_t* config)
{
    return (config != NULL) &&
           servo_core_config_is_valid(&config->servo) &&
           motion_manager_config_is_valid(&config->motion) &&
           step_direction_config_is_valid(&config->step_direction);
}

bool application_core_init(application_core_t* application,
                           const application_core_config_t* config)
{
    const servo_core_output_t empty_servo_output = {0};

    if ((application == NULL) ||
        !application_core_config_is_valid(config))
    {
        return false;
    }
    if (!servo_core_init(&application->servo, &config->servo) ||
        !step_direction_init(&application->step_direction,
                             &config->step_direction))
    {
        return false;
    }

    application->config = *config;
    application->last_servo_output = empty_servo_output;
    application->step_direction_command_id = 0u;
    application->motion_ready = false;
    application->control_enabled = false;
    application->initialized = true;
    return true;
}

application_core_status_t application_core_observe_encoder(
    application_core_t* application,
    uint16_t raw_angle,
    uint32_t timestamp_us)
{
    servo_core_status_t status;

    if ((application == NULL) || !application->initialized)
    {
        return APPLICATION_CORE_STATUS_INVALID_ARGUMENT;
    }

    status = servo_core_observe_encoder(&application->servo,
                                        raw_angle,
                                        timestamp_us);
    if (status != SERVO_CORE_STATUS_OK)
    {
        if ((status == SERVO_CORE_STATUS_FAULTED) &&
            application->motion_ready)
        {
            (void)latch_application_fault(application, timestamp_us);
        }
        return (status == SERVO_CORE_STATUS_FAULTED) ?
            APPLICATION_CORE_STATUS_FAULTED :
            APPLICATION_CORE_STATUS_INVALID_ARGUMENT;
    }

    if (!application->motion_ready)
    {
        if (!motion_manager_init(
                &application->motion,
                &application->config.motion,
                application->servo.angle_tracker.position_revolutions) ||
            (servo_core_suspend(&application->servo) !=
             SERVO_CORE_STATUS_OK))
        {
            return APPLICATION_CORE_STATUS_FAULTED;
        }
        application->motion_ready = true;
    }
    return APPLICATION_CORE_STATUS_OK;
}

motion_submit_status_t application_core_submit_motion(
    application_core_t* application,
    const motion_request_t* request,
    uint32_t timestamp_us)
{
    motion_submit_status_t status;
    motion_action_t action;

    if ((application == NULL) || !application->initialized ||
        !application->motion_ready)
    {
        return MOTION_SUBMIT_INVALID_ARGUMENT;
    }

    status = motion_manager_submit(
        &application->motion,
        request,
        timestamp_us,
        application->servo.angle_tracker.position_revolutions,
        &action);
    if ((status == MOTION_SUBMIT_ACCEPTED) &&
        !apply_action(application, &action, timestamp_us))
    {
        (void)latch_application_fault(application, timestamp_us);
        return MOTION_SUBMIT_FAULTED;
    }
    return status;
}

bool application_core_update_step_direction(
    application_core_t* application,
    int32_t cumulative_steps,
    bool enabled,
    uint32_t timestamp_us,
    motion_submit_status_t* submit_status)
{
    step_direction_output_t step_output;
    motion_request_t request = {
        .source = MOTION_SOURCE_STEP_DIRECTION,
    };
    motion_action_t action;

    if ((application == NULL) || (submit_status == NULL) ||
        !application->initialized || !application->motion_ready)
    {
        return false;
    }
    if (!step_direction_update(
            &application->step_direction,
            cumulative_steps,
            enabled,
            timestamp_us,
            application->servo.angle_tracker.position_revolutions,
            &step_output))
    {
        (void)latch_application_fault(application, timestamp_us);
        *submit_status = MOTION_SUBMIT_FAULTED;
        return false;
    }

    if (step_output.event == STEP_DIRECTION_EVENT_NONE)
    {
        *submit_status =
            (application->motion.state == MOTION_STATE_FAULT) ?
                MOTION_SUBMIT_FAULTED : MOTION_SUBMIT_ACCEPTED;
        return true;
    }
    if (step_output.event == STEP_DIRECTION_EVENT_TARGET_UPDATED)
    {
        *submit_status = motion_manager_update_stream_target(
            &application->motion,
            MOTION_SOURCE_STEP_DIRECTION,
            step_output.target_position_revolutions,
            &action);
    }
    else
    {
        request.command_id =
            next_step_direction_command_id(application);
        request.kind = (step_output.event == STEP_DIRECTION_EVENT_ENABLED) ?
            MOTION_COMMAND_ENABLE : MOTION_COMMAND_DISABLE;
        *submit_status = motion_manager_submit(
            &application->motion,
            &request,
            timestamp_us,
            application->servo.angle_tracker.position_revolutions,
            &action);
    }

    if ((*submit_status == MOTION_SUBMIT_ACCEPTED) &&
        !apply_action(application, &action, timestamp_us))
    {
        (void)latch_application_fault(application, timestamp_us);
        *submit_status = MOTION_SUBMIT_FAULTED;
    }
    return true;
}

application_core_status_t application_core_step(
    application_core_t* application,
    uint32_t timestamp_us,
    application_core_output_t* output)
{
    motion_action_t action;
    servo_core_status_t servo_status;
    bool motion_complete;

    if ((application == NULL) || (output == NULL) ||
        !application->initialized)
    {
        return APPLICATION_CORE_STATUS_INVALID_ARGUMENT;
    }
    if (!application->motion_ready)
    {
        return APPLICATION_CORE_STATUS_NOT_READY;
    }

    if (!motion_manager_poll(&application->motion,
                             timestamp_us,
                             false,
                             servo_core_is_faulted(&application->servo),
                             &action) ||
        !apply_action(application, &action, timestamp_us))
    {
        (void)latch_application_fault(application, timestamp_us);
        publish_output(application, output);
        return APPLICATION_CORE_STATUS_FAULTED;
    }

    if (!application->control_enabled)
    {
        application->last_servo_output.valid = false;
        application->last_servo_output.torque_current_request_amperes = 0.0f;
        publish_output(application, output);
        return (output->motion.state == MOTION_STATE_FAULT) ?
            APPLICATION_CORE_STATUS_FAULTED : APPLICATION_CORE_STATUS_OK;
    }

    servo_status = servo_core_step(&application->servo,
                                   timestamp_us,
                                   &application->last_servo_output);
    if (servo_status != SERVO_CORE_STATUS_OK)
    {
        (void)latch_application_fault(application, timestamp_us);
        publish_output(application, output);
        return APPLICATION_CORE_STATUS_FAULTED;
    }

    motion_complete =
        application->last_servo_output.trajectory_settled &&
        (fabsf(application->last_servo_output.reference_position_revolutions -
               application->last_servo_output.measured_position_revolutions) <=
         application->config.servo.motion_profile.
             position_tolerance_revolutions) &&
        (fabsf(application->last_servo_output.
                   measured_velocity_revolutions_per_second) <=
         application->config.servo.motion_profile.
             velocity_tolerance_revolutions_per_second);
    if (!motion_manager_poll(&application->motion,
                             timestamp_us,
                             motion_complete,
                             false,
                             &action) ||
        !apply_action(application, &action, timestamp_us))
    {
        (void)latch_application_fault(application, timestamp_us);
        publish_output(application, output);
        return APPLICATION_CORE_STATUS_FAULTED;
    }

    publish_output(application, output);
    return (output->motion.state == MOTION_STATE_FAULT) ?
        APPLICATION_CORE_STATUS_FAULTED : APPLICATION_CORE_STATUS_OK;
}

bool application_core_recover(application_core_t* application,
                              bool safe_to_recover,
                              uint16_t raw_angle,
                              uint32_t timestamp_us)
{
    application_core_config_t config;

    if ((application == NULL) || !application->initialized ||
        !safe_to_recover)
    {
        return false;
    }

    config = application->config;
    return application_core_init(application, &config) &&
           (application_core_observe_encoder(application,
                                             raw_angle,
                                             timestamp_us) ==
            APPLICATION_CORE_STATUS_OK);
}
