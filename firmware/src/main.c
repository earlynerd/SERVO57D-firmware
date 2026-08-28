#include <stdbool.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "mks57d/adc1.h"
#include "mks57d/adc_calibration.h"
#include "mks57d/adc_display.h"
#include "mks57d/aligned_torque_controller.h"
#include "mks57d/alignment_controller.h"
#include "mks57d/angle_tracker.h"
#include "mks57d/app_state.h"
#include "mks57d/board.h"
#include "mks57d/board_inputs.h"
#include "mks57d/boot_self_test.h"
#include "mks57d/command_service.h"
#include "mks57d/configuration_flash.h"
#include "mks57d/configuration_store.h"
#include "mks57d/control_math.h"
#include "mks57d/current_loop_backend.h"
#include "mks57d/cycle_counter.h"
#include "mks57d/diagnostics.h"
#include "mks57d/encoder_liveness.h"
#include "mks57d/i2c1.h"
#include "mks57d/interrupt_priority.h"
#include "mks57d/mt6816.h"
#include "mks57d/motor_alignment.h"
#include "mks57d/native_protocol.h"
#include "mks57d/panic.h"
#include "mks57d/platform.h"
#include "mks57d/position_controller.h"
#include "mks57d/rs485.h"
#include "mks57d/rotor_control_runtime.h"
#include "mks57d/runtime_profile.h"
#include "mks57d/spi1.h"
#include "mks57d/ssd1306.h"
#include "mks57d/timebase.h"
#include "mks57d/tim2_current_trigger.h"
#include "mks57d/tim3_bridge_pwm.h"
#include "mks57d/user_inputs.h"
#include "mks57d/velocity_controller.h"
#include "mks57d/watchdog.h"
#include "n32l40x.h"

#if !defined(MKS57D_PRODUCT_IMAGE)
#error "This entry point builds the MKS57D product firmware image"
#endif

_Static_assert((unsigned int)NATIVE_PROTOCOL_MAX_WIRE_FRAME_SIZE <=
                   (unsigned int)RS485_TX_MAX_FRAME_SIZE,
               "native frames must fit the bounded RS-485 TX staging buffer");
_Static_assert((unsigned int)ADC1_SYNCHRONOUS_CURRENT_FREQUENCY_HZ ==
                   (unsigned int)TIM3_BRIDGE_PWM_FREQUENCY_HZ,
               "ADC acquisition rate must match the PWM trigger rate");
_Static_assert((unsigned int)TIM2_CURRENT_TRIGGER_FREQUENCY_HZ ==
                   (unsigned int)TIM3_BRIDGE_PWM_FREQUENCY_HZ,
               "current trigger rate must match the PWM carrier rate");
_Static_assert((unsigned int)RUNTIME_PROFILE_METRIC_COUNT ==
                   (unsigned int)COMMAND_RUNTIME_PROFILE_METRIC_COUNT,
               "runtime profile metrics must match the command schema");

enum
{
    DISPLAY_WIDTH = 72u,
    DISPLAY_HEIGHT = 40u,
    DISPLAY_FRAME_BYTES = DISPLAY_WIDTH * (DISPLAY_HEIGHT / 8u)
};

static uint8_t s_display_frame[DISPLAY_FRAME_BYTES];
static rotor_control_runtime_t rotor_control_runtime;
static rotor_control_snapshot_t rotor_control_snapshot;
static rotor_control_progress_snapshot_t rotor_control_progress_snapshot;

enum
{
    COMMISSIONING_STATUS_SCHEMA_VERSION = 5u,
    ENCODER_STATUS_SCHEMA_VERSION = 2u,
    CURRENT_TRACE_SCHEMA_VERSION = 2u,
    ALIGNMENT_STATUS_SCHEMA_VERSION = 1u,
    ALIGNED_TORQUE_STATUS_SCHEMA_VERSION = 2u,
    VELOCITY_STATUS_SCHEMA_VERSION = 1u,
    POSITION_STATUS_SCHEMA_VERSION = 1u,
    FAULT_RECOVERY_STATUS_SCHEMA_VERSION = 1u,
    CURRENT_TEST_MINIMUM_FREQUENCY_MILLIHZ = 1u,
    CURRENT_TEST_MAXIMUM_FREQUENCY_MILLIHZ = 1000000u,
    CURRENT_TEST_MINIMUM_REMOTE_DURATION_MS = 3u,
    CURRENT_TEST_MAXIMUM_REMOTE_DURATION_MS = INT32_MAX,
    CURRENT_TEST_REFERENCE_FREQUENCY_HZ =
        ADC1_SYNCHRONOUS_CURRENT_FREQUENCY_HZ,
    CURRENT_TEST_INITIAL_LEG_A1 = 0u,
    CURRENT_TEST_INITIAL_LEG_A2 = 1u,
    CURRENT_TEST_INITIAL_LEG_B1 = 2u,
    CURRENT_TEST_INITIAL_LEG_B2 = 3u,
    CURRENT_TEST_INITIAL_LEG_COUNT = 4u,
    ALIGNMENT_MINIMUM_CURRENT_COUNTS = 50u,
    ENCODER_ESTIMATOR_FAULT_INVALID_SAMPLE = 1u << 0
};

/* Foreground command-adapter wiring and request mailboxes. This aggregate does
 * not own rotor estimation, bridge state, or a control loop; new motion state
 * belongs in a product motion service rather than growing this context. */
typedef struct product_command_context product_command_context_t;

typedef struct
{
    product_command_context_t* product;
    app_supervisor_t* supervisor;
    bool* bridge_ready;
    diagnostics_encoder_t* encoder_diagnostics;
    angle_tracker_t* angle_tracker;
    uint32_t* estimator_fault_flags;
    motor_alignment_t* motor_alignment;
    alignment_controller_t* alignment_controller;
    aligned_torque_controller_t* aligned_torque_controller;
    velocity_controller_t* velocity_controller;
    rotor_control_runtime_t* rotor_control_runtime;
    uint32_t* raw_input_levels;
    int32_t maximum_command_velocity_revolutions_per_second_q16_16;
    int32_t default_command_acceleration_revolutions_per_second2_q16_16;
    int32_t requested_velocity_revolutions_per_second_q16_16;
    int32_t requested_acceleration_revolutions_per_second2_q16_16;
    uint16_t requested_current_limit_counts;
    uint32_t requested_duration_millis;
    bool start_requested;
    bool stop_requested;
} velocity_command_context_t;

typedef struct
{
    product_command_context_t* product;
    app_supervisor_t* supervisor;
    bool* bridge_ready;
    diagnostics_encoder_t* encoder_diagnostics;
    angle_tracker_t* angle_tracker;
    uint32_t* estimator_fault_flags;
    motor_alignment_t* motor_alignment;
    alignment_controller_t* alignment_controller;
    aligned_torque_controller_t* aligned_torque_controller;
    velocity_controller_t* velocity_controller;
    position_controller_t* position_controller;
    rotor_control_runtime_t* rotor_control_runtime;
    uint32_t* raw_input_levels;
    int32_t requested_displacement_revolutions_q16_16;
    int32_t requested_maximum_velocity_q16_16;
    int32_t requested_maximum_acceleration_q16_16;
    uint16_t requested_current_limit_counts;
    uint32_t requested_duration_millis;
    bool start_requested;
    bool stop_requested;
} position_command_context_t;

struct product_command_context
{
    app_supervisor_t* supervisor;
    bool* adc_ready;
    bool* adc_snapshot_valid;
    bool* adc_calibration_ready;
    bool* current_loop_initialized;
    uint16_t* current_loop_fault_code;
    bool* bridge_ready;
    adc1_status_t* adc_status;
    adc1_current_snapshot_t* adc_snapshot;
    bool* vbus_snapshot_valid;
    adc1_vbus_snapshot_t* vbus_snapshot;
    adc_calibration_t* adc_calibration;
    diagnostics_encoder_t* encoder_diagnostics;
    angle_tracker_t* angle_tracker;
    bool* encoder_feedback_live;
    uint32_t* estimator_fault_flags;
    uint32_t* estimator_sample_interval_us;
    uint32_t* estimator_maximum_sample_interval_us;
    motor_alignment_t* motor_alignment;
    alignment_controller_t* alignment_controller;
    aligned_torque_controller_t* aligned_torque_controller;
    velocity_controller_t* velocity_controller;
    rotor_control_runtime_t* rotor_control_runtime;
    configuration_store_t* configuration_store;
    uint32_t* raw_input_levels;
    uint32_t* input_levels;
    phase_current_loop_config_t* current_loop_config;
    uint16_t maximum_test_amplitude_counts;
    uint16_t test_amplitude_counts;
    uint32_t test_frequency_millihz;
    uint8_t test_controller_mode;
    bool remote_start_requested;
    bool remote_stop_requested;
    bool remote_authority_active;
    uint8_t remote_start_leg;
    uint32_t remote_start_ramp_duration_millis;
    uint32_t remote_start_duration_millis;
    uint32_t remote_run_deadline_millis;
    uint16_t alignment_current_counts;
    bool alignment_start_requested;
    bool alignment_stop_requested;
    int16_t torque_q_current_counts;
    uint32_t torque_duration_millis;
    bool torque_start_requested;
    bool torque_stop_requested;
    velocity_command_context_t* velocity_commands;
    position_command_context_t* position_commands;
};

static bool encoder_control_ready(
    const diagnostics_encoder_t* encoder_diagnostics,
    const angle_tracker_t* angle_tracker,
    bool encoder_feedback_live,
    uint32_t estimator_fault_flags)
{
    return (encoder_diagnostics != NULL) &&
           (angle_tracker != NULL) &&
           (encoder_diagnostics->status == (uint32_t)MT6816_STATUS_OK) &&
           (encoder_diagnostics->transport_status ==
            (uint32_t)SPI_STATUS_OK) &&
           (encoder_diagnostics->flags == 0u) &&
           encoder_feedback_live &&
           angle_tracker->initialized &&
           (estimator_fault_flags == 0u);
}

static bool velocity_request_or_control_active(
    const product_command_context_t* commands)
{
    return (commands != NULL) && (commands->velocity_commands != NULL) &&
           (commands->velocity_commands->start_requested ||
            velocity_controller_is_active(
                commands->velocity_commands->velocity_controller));
}

static bool position_request_or_control_active(
    const product_command_context_t* commands)
{
    return (commands != NULL) && (commands->position_commands != NULL) &&
           (commands->position_commands->start_requested ||
            position_controller_is_active(
                commands->position_commands->position_controller));
}

static uint32_t current_test_initial_phase(uint8_t selected_leg)
{
    switch (selected_leg)
    {
        case CURRENT_TEST_INITIAL_LEG_A1:
            return 0x80000000u;
        case CURRENT_TEST_INITIAL_LEG_A2:
            return 0x00000000u;
        case CURRENT_TEST_INITIAL_LEG_B1:
            return 0x40000000u;
        case CURRENT_TEST_INITIAL_LEG_B2:
            return 0xC0000000u;
        default:
            return 0u;
    }
}

static uint32_t current_test_phase_increment(uint32_t frequency_millihz)
{
    const uint64_t phase_increment_denominator =
        (uint64_t)CURRENT_TEST_REFERENCE_FREQUENCY_HZ * 1000u;
    const uint64_t numerator =
        ((uint64_t)frequency_millihz << 32u) +
        phase_increment_denominator / 2u;

    return (uint32_t)(numerator / phase_increment_denominator);
}

static uint64_t current_test_ramp_step_count(uint32_t ramp_duration_millis)
{
    return (uint64_t)ramp_duration_millis *
        CURRENT_TEST_REFERENCE_FREQUENCY_HZ / 1000u;
}

static command_status_t commissioning_get_status(
    void* context,
    command_commissioning_status_t* status)
{
    product_command_context_t* commissioning = context;
    current_loop_backend_snapshot_t loop = {0};
    uint32_t now;

    if ((commissioning == NULL) || (status == NULL))
    {
        return COMMAND_STATUS_INTERNAL_ERROR;
    }

    current_loop_backend_get_snapshot(&loop);
    memset(status, 0, sizeof(*status));
    status->schema_version = COMMISSIONING_STATUS_SCHEMA_VERSION;
    if (*commissioning->adc_ready)
    {
        status->flags |= COMMAND_COMMISSIONING_FLAG_ADC_READY;
    }
    if (*commissioning->adc_snapshot_valid)
    {
        status->flags |= COMMAND_COMMISSIONING_FLAG_ADC_SNAPSHOT_VALID;
    }
    if (*commissioning->adc_calibration_ready)
    {
        status->flags |= COMMAND_COMMISSIONING_FLAG_ADC_CALIBRATION_READY;
    }
    if (*commissioning->current_loop_initialized)
    {
        status->flags |=
            COMMAND_COMMISSIONING_FLAG_CURRENT_LOOP_INITIALIZED;
    }
    if (*commissioning->bridge_ready)
    {
        status->flags |= COMMAND_COMMISSIONING_FLAG_BRIDGE_READY;
    }
    if (app_supervisor_bridge_authorized(commissioning->supervisor))
    {
        status->flags |= COMMAND_COMMISSIONING_FLAG_AUTHORITY_ACTIVE;
    }
    if (loop.active)
    {
        status->flags |= COMMAND_COMMISSIONING_FLAG_BACKEND_ACTIVE;
    }
    if (commissioning->remote_authority_active)
    {
        status->flags |= COMMAND_COMMISSIONING_FLAG_REMOTE_AUTHORITY;
    }
    if (commissioning->remote_start_requested)
    {
        status->flags |= COMMAND_COMMISSIONING_FLAG_REMOTE_START_PENDING;
    }
    if (commissioning->remote_stop_requested)
    {
        status->flags |= COMMAND_COMMISSIONING_FLAG_REMOTE_STOP_PENDING;
    }
    if ((loop.fault_flags != 0u) ||
        ((commissioning->supervisor != NULL) &&
         (commissioning->supervisor->state == APP_STATE_FAULT)))
    {
        status->flags |= COMMAND_COMMISSIONING_FLAG_FAULT_PRESENT;
    }
    if (*commissioning->vbus_snapshot_valid)
    {
        status->flags |=
            COMMAND_COMMISSIONING_FLAG_VBUS_SNAPSHOT_VALID;
    }

    status->raw_input_levels =
        (uint8_t)*commissioning->raw_input_levels;
    status->debounced_input_levels =
        (uint8_t)*commissioning->input_levels;
    status->adc_status = (uint8_t)*commissioning->adc_status;
    status->selected_leg = commissioning->remote_start_leg;
    status->fault_flags = loop.fault_flags;
    status->sample_count = loop.sample_count;
    status->current_a_raw = commissioning->adc_snapshot->current_a_raw;
    status->current_b_raw = commissioning->adc_snapshot->current_b_raw;
    if (*commissioning->adc_calibration_ready)
    {
        status->current_a_zero_raw = (uint16_t)(
            commissioning->adc_calibration->current_a_zero_raw + 0.5f);
        status->current_b_zero_raw = (uint16_t)(
            commissioning->adc_calibration->current_b_zero_raw + 0.5f);
    }
    status->current_a_reference_counts =
        loop.current_a_reference_counts;
    status->current_b_reference_counts =
        loop.current_b_reference_counts;
    /* Normal inactive status describes currently applied output. A faulted
       backend retains its final sample so the initiating evidence survives. */
    if (loop.active || (loop.fault_flags != 0u))
    {
        status->current_a_measured_counts =
            loop.latest_output.current_a_measured_counts;
        status->current_b_measured_counts =
            loop.latest_output.current_b_measured_counts;
        status->phase_a_voltage_permille =
            loop.latest_output.phase_a_voltage_permille;
        status->phase_b_voltage_permille =
            loop.latest_output.phase_b_voltage_permille;
        status->duty_a1_permille = loop.latest_output.duty_permille[0];
        status->duty_a2_permille = loop.latest_output.duty_permille[1];
        status->duty_b1_permille = loop.latest_output.duty_permille[2];
        status->duty_b2_permille = loop.latest_output.duty_permille[3];
    }
    status->test_amplitude_counts =
        commissioning->test_amplitude_counts;
    status->maximum_test_amplitude_counts =
        commissioning->maximum_test_amplitude_counts;
    status->hard_current_limit_counts =
        commissioning->current_loop_config->hard_current_limit_counts;
    status->phase_voltage_limit_permille =
        commissioning->current_loop_config->phase_voltage_limit_permille;
    status->test_frequency_millihz =
        commissioning->test_frequency_millihz;
    status->missed_pwm_update_count =
        loop.missed_pwm_update_count;
    status->maximum_consecutive_missed_pwm_updates =
        loop.maximum_consecutive_missed_pwm_updates;
    status->test_controller_mode = commissioning->test_controller_mode;

    now = timebase_millis();
    if (commissioning->remote_authority_active &&
        ((int32_t)(commissioning->remote_run_deadline_millis - now) > 0))
    {
        status->remote_run_remaining_millis =
            commissioning->remote_run_deadline_millis - now;
    }
    status->retained_panic = (uint8_t)g_diagnostics.retained_panic;
    status->watchdog_reset =
        (g_platform_boot_diagnostics.reset_flags &
         RCC_CTRLSTS_IWDGRSTF) != 0u ? 1u : 0u;
    if (*commissioning->vbus_snapshot_valid)
    {
        status->vbus_raw = commissioning->vbus_snapshot->vbus_raw;
        status->vbus_sample_count =
            commissioning->vbus_snapshot->sample_count;
    }
    return COMMAND_STATUS_OK;
}

static command_status_t commissioning_configure(
    void* context,
    const command_current_test_config_t* requested,
    command_current_test_config_t* applied)
{
    product_command_context_t* commissioning = context;

    if ((commissioning == NULL) || (requested == NULL) ||
        (applied == NULL))
    {
        return COMMAND_STATUS_INTERNAL_ERROR;
    }
    if ((requested->amplitude_counts == 0u) ||
        (requested->amplitude_counts >
         commissioning->maximum_test_amplitude_counts) ||
        (requested->frequency_millihz <
         CURRENT_TEST_MINIMUM_FREQUENCY_MILLIHZ) ||
        (requested->frequency_millihz >
         CURRENT_TEST_MAXIMUM_FREQUENCY_MILLIHZ) ||
        (requested->controller_mode >=
         COMMAND_CURRENT_TEST_CONTROLLER_COUNT))
    {
        return COMMAND_STATUS_INVALID_PAYLOAD;
    }
    if (app_supervisor_bridge_authorized(commissioning->supervisor) ||
        commissioning->remote_start_requested ||
        commissioning->alignment_start_requested ||
        commissioning->torque_start_requested ||
        velocity_request_or_control_active(commissioning) ||
        aligned_torque_controller_is_active(
            commissioning->aligned_torque_controller) ||
        alignment_controller_is_active(
            commissioning->alignment_controller))
    {
        return COMMAND_STATUS_UNAVAILABLE;
    }

    commissioning->test_amplitude_counts = requested->amplitude_counts;
    commissioning->test_frequency_millihz = requested->frequency_millihz;
    commissioning->test_controller_mode = requested->controller_mode;
    *applied = *requested;
    return COMMAND_STATUS_OK;
}

static command_status_t commissioning_start(
    void* context,
    uint8_t selected_leg,
    uint32_t ramp_duration_millis,
    uint32_t duration_millis)
{
    product_command_context_t* commissioning = context;
    current_loop_backend_snapshot_t loop = {0};
    const uint64_t total_duration_millis =
        (uint64_t)ramp_duration_millis + duration_millis;

    if (commissioning == NULL)
    {
        return COMMAND_STATUS_INTERNAL_ERROR;
    }
    if ((selected_leg >= CURRENT_TEST_INITIAL_LEG_COUNT) ||
        (duration_millis < CURRENT_TEST_MINIMUM_REMOTE_DURATION_MS) ||
        (duration_millis > CURRENT_TEST_MAXIMUM_REMOTE_DURATION_MS) ||
        (ramp_duration_millis >
         CURRENT_TEST_MAXIMUM_REMOTE_DURATION_MS) ||
        (total_duration_millis >
         CURRENT_TEST_MAXIMUM_REMOTE_DURATION_MS))
    {
        return COMMAND_STATUS_INVALID_PAYLOAD;
    }

    current_loop_backend_get_snapshot(&loop);
    if ((commissioning->supervisor == NULL) ||
        (commissioning->supervisor->state != APP_STATE_READY) ||
        (commissioning->supervisor->authority != APP_AUTHORITY_NONE) ||
        !*commissioning->bridge_ready ||
        app_supervisor_bridge_authorized(commissioning->supervisor) ||
        commissioning->remote_start_requested ||
        commissioning->remote_stop_requested ||
        commissioning->alignment_start_requested ||
        commissioning->alignment_stop_requested ||
        commissioning->torque_start_requested ||
        commissioning->torque_stop_requested ||
        velocity_request_or_control_active(commissioning) ||
        ((commissioning->velocity_commands != NULL) &&
         commissioning->velocity_commands->stop_requested) ||
        position_request_or_control_active(commissioning) ||
        ((commissioning->position_commands != NULL) &&
         commissioning->position_commands->stop_requested) ||
        aligned_torque_controller_is_active(
            commissioning->aligned_torque_controller) ||
        alignment_controller_is_active(
            commissioning->alignment_controller) ||
        (loop.fault_flags != 0u) ||
        ((*commissioning->raw_input_levels & USER_INPUT_BUTTON_RIGHT) == 0u))
    {
        return COMMAND_STATUS_UNAVAILABLE;
    }

    commissioning->remote_start_leg = selected_leg;
    commissioning->remote_start_ramp_duration_millis =
        ramp_duration_millis;
    commissioning->remote_start_duration_millis = duration_millis;
    commissioning->remote_stop_requested = false;
    commissioning->remote_start_requested = true;
    return COMMAND_STATUS_OK;
}

static command_status_t commissioning_stop(void* context)
{
    product_command_context_t* commissioning = context;

    if (commissioning == NULL)
    {
        return COMMAND_STATUS_INTERNAL_ERROR;
    }

    commissioning->remote_start_requested = false;
    commissioning->remote_stop_requested = true;
    commissioning->alignment_start_requested = false;
    commissioning->alignment_stop_requested = true;
    commissioning->torque_start_requested = false;
    commissioning->torque_stop_requested = true;
    if (commissioning->velocity_commands != NULL)
    {
        commissioning->velocity_commands->start_requested = false;
        commissioning->velocity_commands->stop_requested = true;
    }
    if (commissioning->position_commands != NULL)
    {
        commissioning->position_commands->start_requested = false;
        commissioning->position_commands->stop_requested = true;
    }
    return COMMAND_STATUS_OK;
}

static uint32_t fault_sources_from_rotor_snapshot(
    const rotor_control_snapshot_t* snapshot)
{
    uint32_t sources = 0u;

    if (snapshot == NULL)
    {
        return 0u;
    }
    if (snapshot->estimator_fault_flags != 0u)
    {
        sources |= COMMAND_FAULT_SOURCE_ESTIMATOR;
    }
    if (snapshot->alignment_controller.status.state ==
        ALIGNMENT_CONTROLLER_STATE_FAILED)
    {
        sources |= COMMAND_FAULT_SOURCE_ALIGNMENT;
    }
    if ((snapshot->torque_controller.status.state ==
         ALIGNED_TORQUE_STATE_FAILED) ||
        (snapshot->torque_controller.status.fault_flags != 0u))
    {
        sources |= COMMAND_FAULT_SOURCE_ALIGNED_TORQUE;
    }
    if ((snapshot->velocity_controller.status.state ==
         VELOCITY_CONTROL_STATE_FAILED) ||
        (snapshot->velocity_controller.status.fault_flags != 0u))
    {
        sources |= COMMAND_FAULT_SOURCE_VELOCITY;
    }
    if ((snapshot->position_controller.status.state ==
         POSITION_CONTROL_STATE_FAILED) ||
        (snapshot->position_controller.status.fault_flags != 0u))
    {
        sources |= COMMAND_FAULT_SOURCE_POSITION;
    }
    return sources;
}

static uint32_t command_sources_from_runtime_sources(
    uint32_t runtime_sources)
{
    uint32_t sources = 0u;

    if ((runtime_sources & ROTOR_CONTROL_FAULT_SOURCE_ESTIMATOR) != 0u)
    {
        sources |= COMMAND_FAULT_SOURCE_ESTIMATOR;
    }
    if ((runtime_sources & ROTOR_CONTROL_FAULT_SOURCE_ALIGNMENT) != 0u)
    {
        sources |= COMMAND_FAULT_SOURCE_ALIGNMENT;
    }
    if ((runtime_sources &
         ROTOR_CONTROL_FAULT_SOURCE_ALIGNED_TORQUE) != 0u)
    {
        sources |= COMMAND_FAULT_SOURCE_ALIGNED_TORQUE;
    }
    if ((runtime_sources & ROTOR_CONTROL_FAULT_SOURCE_VELOCITY) != 0u)
    {
        sources |= COMMAND_FAULT_SOURCE_VELOCITY;
    }
    if ((runtime_sources & ROTOR_CONTROL_FAULT_SOURCE_POSITION) != 0u)
    {
        sources |= COMMAND_FAULT_SOURCE_POSITION;
    }
    return sources;
}

static void clear_command_mailboxes(product_command_context_t* commands)
{
    commands->remote_start_requested = false;
    commands->remote_stop_requested = false;
    commands->remote_authority_active = false;
    commands->alignment_start_requested = false;
    commands->alignment_stop_requested = false;
    commands->torque_start_requested = false;
    commands->torque_stop_requested = false;
    if (commands->velocity_commands != NULL)
    {
        commands->velocity_commands->start_requested = false;
        commands->velocity_commands->stop_requested = false;
    }
    if (commands->position_commands != NULL)
    {
        commands->position_commands->start_requested = false;
        commands->position_commands->stop_requested = false;
    }
}

static command_status_t drive_clear_faults(
    void* context,
    command_fault_recovery_status_t* status)
{
    product_command_context_t* commands = context;
    current_loop_backend_snapshot_t loop = {0};
    rotor_control_snapshot_t rotor = {0};
    uint32_t sources;
    uint32_t runtime_sources = 0u;
    uint32_t backend_faults = 0u;

    if ((commands == NULL) || (status == NULL) ||
        (commands->supervisor == NULL) ||
        (commands->rotor_control_runtime == NULL) ||
        (commands->bridge_ready == NULL) ||
        (commands->adc_ready == NULL) ||
        (commands->adc_snapshot_valid == NULL) ||
        (commands->vbus_snapshot_valid == NULL) ||
        (commands->adc_status == NULL) ||
        (commands->angle_tracker == NULL) ||
        (commands->encoder_feedback_live == NULL) ||
        (commands->estimator_fault_flags == NULL) ||
        (commands->estimator_sample_interval_us == NULL) ||
        (commands->estimator_maximum_sample_interval_us == NULL) ||
        (commands->alignment_controller == NULL) ||
        (commands->aligned_torque_controller == NULL) ||
        (commands->velocity_controller == NULL) ||
        (commands->current_loop_fault_code == NULL) ||
        (commands->position_commands == NULL) ||
        (commands->position_commands->position_controller == NULL))
    {
        return COMMAND_STATUS_INTERNAL_ERROR;
    }

    memset(status, 0, sizeof(*status));
    status->schema_version = FAULT_RECOVERY_STATUS_SCHEMA_VERSION;
    current_loop_backend_get_snapshot(&loop);
    if (!rotor_control_runtime_get_snapshot(
            commands->rotor_control_runtime, &rotor))
    {
        return COMMAND_STATUS_INTERNAL_ERROR;
    }

    sources = fault_sources_from_rotor_snapshot(&rotor);
    if (loop.fault_flags != 0u)
    {
        sources |= COMMAND_FAULT_SOURCE_CURRENT_BACKEND;
    }
    if (commands->supervisor->state == APP_STATE_FAULT)
    {
        sources |= COMMAND_FAULT_SOURCE_SUPERVISOR;
    }
    if (sources == 0u)
    {
        status->result = COMMAND_FAULT_RECOVERY_RESULT_NO_FAULT;
        return COMMAND_STATUS_OK;
    }

    status->result = COMMAND_FAULT_RECOVERY_RESULT_BLOCKED;
    status->remaining_fault_flags = sources;
    clear_command_mailboxes(commands);
    *commands->bridge_ready = false;

    if (!current_loop_backend_recover(&backend_faults))
    {
        status->blocker_flags |=
            COMMAND_FAULT_RECOVERY_BLOCKER_BACKEND_RESET_FAILED;
        status->remaining_fault_flags |=
            COMMAND_FAULT_SOURCE_CURRENT_BACKEND;
        return COMMAND_STATUS_OK;
    }
    if (backend_faults != 0u)
    {
        status->cleared_fault_flags |=
            COMMAND_FAULT_SOURCE_CURRENT_BACKEND;
        status->remaining_fault_flags &=
            ~((uint32_t)COMMAND_FAULT_SOURCE_CURRENT_BACKEND);
    }

    if (!rotor_control_runtime_clear_faults(
            commands->rotor_control_runtime, &runtime_sources))
    {
        status->blocker_flags |=
            COMMAND_FAULT_RECOVERY_BLOCKER_RUNTIME_RESET_FAILED;
        return COMMAND_STATUS_OK;
    }
    status->cleared_fault_flags |=
        command_sources_from_runtime_sources(runtime_sources);
    status->remaining_fault_flags &=
        ~command_sources_from_runtime_sources(runtime_sources);

    if ((commands->supervisor->state != APP_STATE_FAULT) &&
        !app_supervisor_handle_event(
            commands->supervisor,
            APP_EVENT_FAULT_DETECTED,
            (app_transition_context_t){0}))
    {
        status->blocker_flags |=
            COMMAND_FAULT_RECOVERY_BLOCKER_SUPERVISOR_RESET_FAILED;
        return COMMAND_STATUS_OK;
    }
    if (!app_supervisor_handle_event(
            commands->supervisor,
            APP_EVENT_FAULT_ACKNOWLEDGED,
            (app_transition_context_t){.safe_to_recover = true}))
    {
        status->blocker_flags |=
            COMMAND_FAULT_RECOVERY_BLOCKER_SUPERVISOR_RESET_FAILED;
        return COMMAND_STATUS_OK;
    }

    if (!rotor_control_runtime_get_snapshot(
            commands->rotor_control_runtime, &rotor))
    {
        status->blocker_flags |=
            COMMAND_FAULT_RECOVERY_BLOCKER_RUNTIME_RESET_FAILED;
        return COMMAND_STATUS_OK;
    }
    *commands->angle_tracker = rotor.angle_tracker;
    *commands->alignment_controller = rotor.alignment_controller;
    *commands->aligned_torque_controller = rotor.torque_controller;
    *commands->velocity_controller = rotor.velocity_controller;
    *commands->position_commands->position_controller =
        rotor.position_controller;
    *commands->estimator_fault_flags = rotor.estimator_fault_flags;
    *commands->estimator_sample_interval_us =
        rotor.estimator_sample_interval_us;
    *commands->estimator_maximum_sample_interval_us =
        rotor.estimator_maximum_sample_interval_us;
    if ((sources & COMMAND_FAULT_SOURCE_ESTIMATOR) != 0u)
    {
        *commands->encoder_feedback_live = false;
    }

    *commands->adc_ready = true;
    *commands->adc_snapshot_valid = false;
    *commands->vbus_snapshot_valid = false;
    *commands->adc_status = ADC1_STATUS_NO_SAMPLE;
    *commands->current_loop_fault_code = 0u;
    *commands->bridge_ready = true;
    status->cleared_fault_flags |=
        sources & COMMAND_FAULT_SOURCE_SUPERVISOR;
    status->remaining_fault_flags = 0u;
    status->result = COMMAND_FAULT_RECOVERY_RESULT_CLEARED;
    return COMMAND_STATUS_OK;
}

static command_status_t alignment_start(
    void* context,
    uint16_t alignment_current_counts)
{
    product_command_context_t* commissioning = context;
    current_loop_backend_snapshot_t loop = {0};

    if ((commissioning == NULL) ||
        (commissioning->alignment_controller == NULL))
    {
        return COMMAND_STATUS_INTERNAL_ERROR;
    }
    if ((alignment_current_counts < ALIGNMENT_MINIMUM_CURRENT_COUNTS) ||
        (alignment_current_counts >
         commissioning->maximum_test_amplitude_counts))
    {
        return COMMAND_STATUS_INVALID_PAYLOAD;
    }
    current_loop_backend_get_snapshot(&loop);
    if ((commissioning->supervisor == NULL) ||
        (commissioning->supervisor->state != APP_STATE_READY) ||
        (commissioning->supervisor->authority != APP_AUTHORITY_NONE) ||
        !*commissioning->bridge_ready ||
        app_supervisor_bridge_authorized(commissioning->supervisor) ||
        commissioning->remote_start_requested ||
        commissioning->remote_stop_requested ||
        commissioning->alignment_start_requested ||
        commissioning->alignment_stop_requested ||
        commissioning->torque_start_requested ||
        commissioning->torque_stop_requested ||
        velocity_request_or_control_active(commissioning) ||
        ((commissioning->velocity_commands != NULL) &&
         commissioning->velocity_commands->stop_requested) ||
        position_request_or_control_active(commissioning) ||
        ((commissioning->position_commands != NULL) &&
         commissioning->position_commands->stop_requested) ||
        aligned_torque_controller_is_active(
            commissioning->aligned_torque_controller) ||
        alignment_controller_is_active(
            commissioning->alignment_controller) ||
        (loop.fault_flags != 0u) ||
        ((*commissioning->raw_input_levels & USER_INPUT_BUTTON_RIGHT) == 0u))
    {
        return COMMAND_STATUS_UNAVAILABLE;
    }
    commissioning->alignment_current_counts =
        alignment_current_counts;
    commissioning->alignment_stop_requested = false;
    commissioning->alignment_start_requested = true;
    return COMMAND_STATUS_OK;
}

static command_status_t alignment_get_status(
    void* context,
    command_alignment_status_t* status)
{
    product_command_context_t* commissioning = context;
    alignment_controller_status_t controller_status;
    motor_alignment_status_t alignment_status;
    current_loop_backend_snapshot_t loop = {0};

    if ((commissioning == NULL) || (status == NULL) ||
        (commissioning->alignment_controller == NULL) ||
        (commissioning->motor_alignment == NULL) ||
        (commissioning->supervisor == NULL))
    {
        return COMMAND_STATUS_INTERNAL_ERROR;
    }
    alignment_controller_get_status(
        commissioning->alignment_controller, &controller_status);
    motor_alignment_get_status(
        commissioning->motor_alignment, &alignment_status);
    current_loop_backend_get_snapshot(&loop);
    memset(status, 0, sizeof(*status));
    status->schema_version = ALIGNMENT_STATUS_SCHEMA_VERSION;
    status->state = (uint8_t)controller_status.state;
    status->result = (uint8_t)controller_status.result;
    status->alignment_current_counts =
        controller_status.alignment_current_counts;
    status->phase_zero_raw = controller_status.phase_zero_raw;
    status->phase_quarter_raw =
        controller_status.phase_quarter_raw;
    status->return_zero_raw = controller_status.return_zero_raw;
    status->observed_quarter_step_counts =
        controller_status.observed_quarter_step_counts;
    status->quarter_step_error_counts =
        controller_status.quarter_step_error_counts;
    status->closure_error_counts =
        controller_status.closure_error_counts;
    status->encoder_direction = controller_status.encoder_direction;
    if ((controller_status.state ==
         ALIGNMENT_CONTROLLER_STATE_IDLE) && alignment_status.valid)
    {
        status->phase_zero_raw = alignment_status.electrical_zero_raw;
        status->observed_quarter_step_counts =
            alignment_status.observed_quarter_step_counts;
        status->quarter_step_error_counts =
            alignment_status.quarter_step_error_counts;
        status->encoder_direction = alignment_status.encoder_direction;
    }
    status->active_sample_count =
        controller_status.active_sample_count;
    status->elapsed_millis = controller_status.elapsed_millis;
    if (alignment_controller_is_active(
            commissioning->alignment_controller))
    {
        status->flags |= COMMAND_ALIGNMENT_FLAG_ACTIVE;
        if (controller_status.elapsed_millis <
            commissioning->alignment_controller->config.
                maximum_duration_millis)
        {
            status->remaining_millis =
                commissioning->alignment_controller->config.
                    maximum_duration_millis -
                controller_status.elapsed_millis;
        }
    }
    if (alignment_status.valid)
    {
        status->flags |= COMMAND_ALIGNMENT_FLAG_CALIBRATION_VALID;
    }
    if ((commissioning->supervisor->state == APP_STATE_ALIGN) &&
        (commissioning->supervisor->authority == APP_AUTHORITY_MOTION))
    {
        status->flags |= COMMAND_ALIGNMENT_FLAG_AUTHORITY_ACTIVE;
    }
    if (loop.active)
    {
        status->flags |= COMMAND_ALIGNMENT_FLAG_BACKEND_ACTIVE;
    }
    status->minimum_current_counts = ALIGNMENT_MINIMUM_CURRENT_COUNTS;
    status->maximum_current_counts =
        commissioning->maximum_test_amplitude_counts;
    status->expected_quarter_step_counts = (uint16_t)(
        ((uint32_t)commissioning->motor_alignment->config.
             encoder_counts_per_revolution +
         ((uint32_t)commissioning->motor_alignment->config.
              electrical_cycles_per_revolution * 2u)) /
        ((uint32_t)commissioning->motor_alignment->config.
             electrical_cycles_per_revolution * 4u));
    status->maximum_quarter_step_error_counts =
        commissioning->motor_alignment->config.
            maximum_quarter_step_error_counts;
    status->settle_duration_millis =
        commissioning->alignment_controller->config.
            settle_duration_millis;
    status->sample_duration_millis =
        commissioning->alignment_controller->config.
            sample_duration_millis;
    status->maximum_duration_millis =
        commissioning->alignment_controller->config.
            maximum_duration_millis;
    status->minimum_sample_count =
        commissioning->alignment_controller->config.
            minimum_sample_count;
    status->maximum_sample_span_counts =
        commissioning->alignment_controller->config.
            maximum_sample_span_counts;
    status->maximum_closure_error_counts =
        commissioning->alignment_controller->config.
            maximum_closure_error_counts;
    status->maximum_current_error_counts =
        commissioning->alignment_controller->config.
            maximum_current_error_counts;
    return COMMAND_STATUS_OK;
}

static command_status_t aligned_torque_start(
    void* context,
    int16_t q_current_counts,
    uint32_t duration_millis)
{
    product_command_context_t* commissioning = context;
    motor_alignment_status_t alignment_status;
    current_loop_backend_snapshot_t loop = {0};
    int32_t q_current_magnitude = q_current_counts;

    if ((commissioning == NULL) ||
        (commissioning->aligned_torque_controller == NULL) ||
        (commissioning->motor_alignment == NULL) ||
        (commissioning->alignment_controller == NULL) ||
        (commissioning->supervisor == NULL) ||
        (commissioning->bridge_ready == NULL) ||
        (commissioning->encoder_diagnostics == NULL) ||
        (commissioning->angle_tracker == NULL) ||
        (commissioning->encoder_feedback_live == NULL) ||
        (commissioning->estimator_fault_flags == NULL) ||
        (commissioning->raw_input_levels == NULL))
    {
        return COMMAND_STATUS_INTERNAL_ERROR;
    }
    if (q_current_magnitude < 0)
    {
        q_current_magnitude = -q_current_magnitude;
    }
    if ((q_current_counts == 0) ||
        (q_current_magnitude > commissioning->aligned_torque_controller->
             config.maximum_current_counts) ||
        (duration_millis < commissioning->aligned_torque_controller->
             config.minimum_duration_millis) ||
        (duration_millis > commissioning->aligned_torque_controller->
             config.maximum_duration_millis))
    {
        return COMMAND_STATUS_INVALID_PAYLOAD;
    }

    motor_alignment_get_status(
        commissioning->motor_alignment, &alignment_status);
    current_loop_backend_get_snapshot(&loop);
    if ((commissioning->supervisor->state != APP_STATE_READY) ||
        (commissioning->supervisor->authority != APP_AUTHORITY_NONE) ||
        !*commissioning->bridge_ready ||
        !alignment_status.valid ||
        !encoder_control_ready(
            commissioning->encoder_diagnostics,
            commissioning->angle_tracker,
            *commissioning->encoder_feedback_live,
            *commissioning->estimator_fault_flags) ||
        app_supervisor_bridge_authorized(commissioning->supervisor) ||
        commissioning->remote_start_requested ||
        commissioning->remote_stop_requested ||
        commissioning->alignment_start_requested ||
        commissioning->alignment_stop_requested ||
        commissioning->torque_start_requested ||
        commissioning->torque_stop_requested ||
        velocity_request_or_control_active(commissioning) ||
        ((commissioning->velocity_commands != NULL) &&
         commissioning->velocity_commands->stop_requested) ||
        position_request_or_control_active(commissioning) ||
        ((commissioning->position_commands != NULL) &&
         commissioning->position_commands->stop_requested) ||
        alignment_controller_is_active(
            commissioning->alignment_controller) ||
        aligned_torque_controller_is_active(
            commissioning->aligned_torque_controller) ||
        loop.active || (loop.fault_flags != 0u) ||
        ((*commissioning->raw_input_levels & USER_INPUT_BUTTON_RIGHT) == 0u))
    {
        return COMMAND_STATUS_UNAVAILABLE;
    }

    commissioning->torque_q_current_counts = q_current_counts;
    commissioning->torque_duration_millis = duration_millis;
    commissioning->torque_stop_requested = false;
    commissioning->torque_start_requested = true;
    return COMMAND_STATUS_OK;
}

static command_status_t aligned_torque_get_status(
    void* context,
    command_aligned_torque_status_t* status)
{
    product_command_context_t* commissioning = context;
    aligned_torque_status_t controller_status;
    motor_alignment_status_t alignment_status;
    current_loop_backend_snapshot_t loop = {0};
    uint32_t now;

    if ((commissioning == NULL) || (status == NULL) ||
        (commissioning->aligned_torque_controller == NULL) ||
        (commissioning->motor_alignment == NULL) ||
        (commissioning->supervisor == NULL))
    {
        return COMMAND_STATUS_INTERNAL_ERROR;
    }

    now = timebase_millis();
    aligned_torque_controller_get_status(
        commissioning->aligned_torque_controller, &controller_status);
    motor_alignment_get_status(
        commissioning->motor_alignment, &alignment_status);
    current_loop_backend_get_snapshot(&loop);
    memset(status, 0, sizeof(*status));
    status->schema_version = ALIGNED_TORQUE_STATUS_SCHEMA_VERSION;
    status->state = (uint8_t)controller_status.state;
    status->result = (uint8_t)controller_status.result;
    status->fault_flags = controller_status.fault_flags;
    status->requested_q_current_counts =
        controller_status.requested_q_current_counts;
    status->applied_q_current_counts =
        controller_status.applied_q_current_counts;
    status->current_a_reference_counts =
        loop.current_a_reference_counts;
    status->current_b_reference_counts =
        loop.current_b_reference_counts;
    status->electrical_phase_q32 =
        controller_status.electrical_phase_q32;
    status->velocity_revolutions_per_second_q16_16 =
        controller_status.velocity_revolutions_per_second_q16_16;
    status->acceleration_revolutions_per_second2_q16_16 =
        controller_status.acceleration_revolutions_per_second2_q16_16;
    status->elapsed_millis = controller_status.elapsed_millis;
    if (controller_status.active)
    {
        status->flags |= COMMAND_TORQUE_FLAG_ACTIVE;
        if ((int32_t)(commissioning->aligned_torque_controller->
                          deadline_millis - now) > 0)
        {
            status->remaining_millis =
                commissioning->aligned_torque_controller->deadline_millis -
                now;
        }
    }
    if ((commissioning->supervisor->state == APP_STATE_RUN) &&
        (commissioning->supervisor->authority == APP_AUTHORITY_MOTION))
    {
        status->flags |= COMMAND_TORQUE_FLAG_AUTHORITY_ACTIVE;
    }
    if (loop.active)
    {
        status->flags |= COMMAND_TORQUE_FLAG_BACKEND_ACTIVE;
    }
    if (alignment_status.valid)
    {
        status->flags |= COMMAND_TORQUE_FLAG_ALIGNMENT_VALID;
    }
    if (controller_status.phase_valid)
    {
        status->flags |= COMMAND_TORQUE_FLAG_PHASE_VALID;
    }
    if (controller_status.state == ALIGNED_TORQUE_STATE_HOLDING)
    {
        status->flags |= COMMAND_TORQUE_FLAG_DEMAND_AT_TARGET;
    }
    status->maximum_current_counts = commissioning->aligned_torque_controller->
        config.maximum_current_counts;
    status->maximum_current_slew_counts_per_second =
        commissioning->aligned_torque_controller->config.
            maximum_current_slew_counts_per_second;
    status->maximum_velocity_revolutions_per_second_q16_16 =
        commissioning->aligned_torque_controller->config.
            maximum_velocity_revolutions_per_second_q16_16;
    status->maximum_acceleration_revolutions_per_second2_q16_16 =
        commissioning->aligned_torque_controller->config.
            maximum_acceleration_revolutions_per_second2_q16_16;
    status->maximum_feedback_interval_us =
        commissioning->aligned_torque_controller->config.
            maximum_feedback_interval_us;
    status->minimum_duration_millis = commissioning->aligned_torque_controller->
        config.minimum_duration_millis;
    status->maximum_duration_millis = commissioning->aligned_torque_controller->
        config.maximum_duration_millis;
    status->backend_fault_flags = loop.fault_flags;
    status->phase_prediction_reject_reason =
        loop.phase_prediction_reject_reason;
    status->rejected_phase_prediction_age_us =
        loop.rejected_phase_prediction_age_us;
    status->maximum_observed_phase_prediction_age_us =
        (uint16_t)loop.maximum_observed_phase_prediction_age_us;
    status->maximum_phase_prediction_age_us =
        (uint16_t)loop.maximum_phase_prediction_age_us;
    return COMMAND_STATUS_OK;
}

static command_status_t velocity_start(
    void* context,
    int32_t velocity_revolutions_per_second_q16_16,
    uint16_t current_limit_counts,
    uint32_t duration_millis,
    int32_t acceleration_revolutions_per_second2_q16_16)
{
    velocity_command_context_t* velocity = context;
    product_command_context_t* product;
    motor_alignment_status_t alignment_status;
    current_loop_backend_snapshot_t loop = {0};
    int64_t target_magnitude =
        velocity_revolutions_per_second_q16_16;

    if ((velocity == NULL) || (velocity->product == NULL) ||
        (velocity->product->encoder_feedback_live == NULL) ||
        (velocity->supervisor == NULL) ||
        (velocity->bridge_ready == NULL) ||
        (velocity->encoder_diagnostics == NULL) ||
        (velocity->angle_tracker == NULL) ||
        (velocity->estimator_fault_flags == NULL) ||
        (velocity->motor_alignment == NULL) ||
        (velocity->alignment_controller == NULL) ||
        (velocity->aligned_torque_controller == NULL) ||
        (velocity->velocity_controller == NULL) ||
        (velocity->rotor_control_runtime == NULL) ||
        (velocity->raw_input_levels == NULL))
    {
        return COMMAND_STATUS_INTERNAL_ERROR;
    }
    product = velocity->product;
    if (target_magnitude < 0)
    {
        target_magnitude = -target_magnitude;
    }
    if (acceleration_revolutions_per_second2_q16_16 == 0)
    {
        acceleration_revolutions_per_second2_q16_16 = velocity->
            default_command_acceleration_revolutions_per_second2_q16_16;
    }
    if ((velocity_revolutions_per_second_q16_16 == 0) ||
        (target_magnitude > velocity->
             maximum_command_velocity_revolutions_per_second_q16_16) ||
        (acceleration_revolutions_per_second2_q16_16 <= 0) ||
        (acceleration_revolutions_per_second2_q16_16 >
         float_to_q16_16(velocity->velocity_controller->config.
             maximum_target_acceleration_revolutions_per_second_squared)) ||
        (current_limit_counts == 0u) ||
        (current_limit_counts > velocity->velocity_controller->config.
             maximum_current_counts) ||
        (duration_millis < velocity->velocity_controller->config.
             minimum_duration_millis) ||
        (duration_millis > velocity->velocity_controller->config.
             maximum_duration_millis))
    {
        return COMMAND_STATUS_INVALID_PAYLOAD;
    }

    motor_alignment_get_status(
        velocity->motor_alignment, &alignment_status);
    current_loop_backend_get_snapshot(&loop);
    if ((velocity->supervisor->state != APP_STATE_READY) ||
        (velocity->supervisor->authority != APP_AUTHORITY_NONE) ||
        !*velocity->bridge_ready || !alignment_status.valid ||
        !encoder_control_ready(
            velocity->encoder_diagnostics,
            velocity->angle_tracker,
            *product->encoder_feedback_live,
            *velocity->estimator_fault_flags) ||
        app_supervisor_bridge_authorized(velocity->supervisor) ||
        product->remote_start_requested ||
        product->remote_stop_requested ||
        product->alignment_start_requested ||
        product->alignment_stop_requested ||
        product->torque_start_requested ||
        product->torque_stop_requested || velocity->start_requested ||
        velocity->stop_requested ||
        position_request_or_control_active(product) ||
        ((product->position_commands != NULL) &&
         product->position_commands->stop_requested) ||
        alignment_controller_is_active(velocity->alignment_controller) ||
        aligned_torque_controller_is_active(
            velocity->aligned_torque_controller) ||
        velocity_controller_is_active(velocity->velocity_controller) ||
        loop.active || (loop.fault_flags != 0u) ||
        ((*velocity->raw_input_levels & USER_INPUT_BUTTON_RIGHT) == 0u))
    {
        return COMMAND_STATUS_UNAVAILABLE;
    }

    velocity->requested_velocity_revolutions_per_second_q16_16 =
        velocity_revolutions_per_second_q16_16;
    velocity->requested_acceleration_revolutions_per_second2_q16_16 =
        acceleration_revolutions_per_second2_q16_16;
    velocity->requested_current_limit_counts = current_limit_counts;
    velocity->requested_duration_millis = duration_millis;
    velocity->stop_requested = false;
    velocity->start_requested = true;
    return COMMAND_STATUS_OK;
}

static command_status_t velocity_get_status(
    void* context,
    command_velocity_status_t* status)
{
    velocity_command_context_t* velocity = context;
    velocity_controller_status_t controller_status;
    aligned_torque_status_t torque_status;
    motor_alignment_status_t alignment_status;
    current_loop_backend_snapshot_t loop = {0};
    int32_t current_magnitude;
    uint32_t now;

    if ((velocity == NULL) || (status == NULL) ||
        (velocity->supervisor == NULL) ||
        (velocity->velocity_controller == NULL) ||
        (velocity->aligned_torque_controller == NULL) ||
        (velocity->motor_alignment == NULL))
    {
        return COMMAND_STATUS_INTERNAL_ERROR;
    }

    now = timebase_millis();
    velocity_controller_get_status(
        velocity->velocity_controller, &controller_status);
    aligned_torque_controller_get_status(
        velocity->aligned_torque_controller, &torque_status);
    motor_alignment_get_status(
        velocity->motor_alignment, &alignment_status);
    current_loop_backend_get_snapshot(&loop);
    memset(status, 0, sizeof(*status));
    status->schema_version = VELOCITY_STATUS_SCHEMA_VERSION;
    status->state = (uint8_t)controller_status.state;
    status->result = (uint8_t)controller_status.result;
    status->fault_flags = controller_status.fault_flags;
    status->target_velocity_revolutions_per_second_q16_16 =
        controller_status.target_velocity_revolutions_per_second_q16_16;
    status->reference_velocity_revolutions_per_second_q16_16 =
        controller_status.reference_velocity_revolutions_per_second_q16_16;
    status->measured_velocity_revolutions_per_second_q16_16 =
        controller_status.measured_velocity_revolutions_per_second_q16_16;
    status->requested_q_current_counts =
        controller_status.requested_q_current_counts;
    if (controller_status.active)
    {
        status->applied_q_current_counts =
            torque_status.applied_q_current_counts;
    }
    status->current_limit_counts = controller_status.current_limit_counts;
    status->elapsed_millis = controller_status.elapsed_millis;
    if (controller_status.active &&
        ((int32_t)(velocity->velocity_controller->deadline_millis - now) > 0))
    {
        status->remaining_millis =
            velocity->velocity_controller->deadline_millis - now;
    }
    if (controller_status.active)
    {
        status->flags |= COMMAND_VELOCITY_FLAG_ACTIVE;
    }
    if (controller_status.active &&
        (velocity->supervisor->state == APP_STATE_RUN) &&
        (velocity->supervisor->authority == APP_AUTHORITY_MOTION))
    {
        status->flags |= COMMAND_VELOCITY_FLAG_AUTHORITY_ACTIVE;
    }
    if (controller_status.active && loop.active)
    {
        status->flags |= COMMAND_VELOCITY_FLAG_BACKEND_ACTIVE;
    }
    if (alignment_status.valid)
    {
        status->flags |= COMMAND_VELOCITY_FLAG_ALIGNMENT_VALID;
    }
    if (controller_status.active && torque_status.active)
    {
        status->flags |= COMMAND_VELOCITY_FLAG_ACTUATOR_ACTIVE;
    }
    if (controller_status.state == VELOCITY_CONTROL_STATE_TRACKING)
    {
        status->flags |= COMMAND_VELOCITY_FLAG_REFERENCE_AT_TARGET;
    }
    current_magnitude = controller_status.requested_q_current_counts;
    if (current_magnitude < 0)
    {
        current_magnitude = -current_magnitude;
    }
    if ((controller_status.current_limit_counts != 0u) &&
        (current_magnitude >= controller_status.current_limit_counts))
    {
        status->flags |= COMMAND_VELOCITY_FLAG_CURRENT_AT_LIMIT;
    }
    status->maximum_target_velocity_revolutions_per_second_q16_16 =
        velocity->maximum_command_velocity_revolutions_per_second_q16_16;
    status->maximum_target_acceleration_revolutions_per_second2_q16_16 =
        float_to_q16_16(velocity->velocity_controller->config.
            maximum_target_acceleration_revolutions_per_second_squared);
    status->maximum_feedback_velocity_revolutions_per_second_q16_16 =
        float_to_q16_16(velocity->velocity_controller->config.
            maximum_feedback_velocity_revolutions_per_second);
    status->maximum_current_counts =
        velocity->velocity_controller->config.maximum_current_counts;
    status->maximum_feedback_interval_us =
        velocity->velocity_controller->config.maximum_feedback_interval_us;
    status->proportional_gain_current_counts_per_velocity_q16_16 =
        float_to_q16_16(velocity->velocity_controller->config.
            current_controller.proportional_gain);
    status->integral_gain_current_counts_per_position_q16_16 =
        float_to_q16_16(velocity->velocity_controller->config.
            current_controller.integral_gain_per_second);
    status->maximum_duration_millis =
        velocity->velocity_controller->config.maximum_duration_millis;
    return COMMAND_STATUS_OK;
}

static command_status_t position_start_relative(
    void* context,
    int32_t displacement_revolutions_q16_16,
    int32_t maximum_velocity_revolutions_per_second_q16_16,
    int32_t maximum_acceleration_revolutions_per_second2_q16_16,
    uint16_t current_limit_counts,
    uint32_t duration_millis)
{
    position_command_context_t* position = context;
    product_command_context_t* product;
    motor_alignment_status_t alignment_status;
    current_loop_backend_snapshot_t loop = {0};
    int64_t displacement_magnitude = displacement_revolutions_q16_16;

    if ((position == NULL) || (position->product == NULL) ||
        (position->product->encoder_feedback_live == NULL) ||
        (position->supervisor == NULL) ||
        (position->bridge_ready == NULL) ||
        (position->encoder_diagnostics == NULL) ||
        (position->angle_tracker == NULL) ||
        (position->estimator_fault_flags == NULL) ||
        (position->motor_alignment == NULL) ||
        (position->alignment_controller == NULL) ||
        (position->aligned_torque_controller == NULL) ||
        (position->velocity_controller == NULL) ||
        (position->position_controller == NULL) ||
        (position->rotor_control_runtime == NULL) ||
        (position->raw_input_levels == NULL))
    {
        return COMMAND_STATUS_INTERNAL_ERROR;
    }
    if (displacement_magnitude < 0)
    {
        displacement_magnitude = -displacement_magnitude;
    }
    if ((displacement_revolutions_q16_16 == 0) ||
        (displacement_magnitude > float_to_q16_16(
            position->position_controller->config.
                maximum_relative_travel_revolutions)) ||
        (maximum_velocity_revolutions_per_second_q16_16 <= 0) ||
        (maximum_velocity_revolutions_per_second_q16_16 >
         float_to_q16_16(position->position_controller->config.
             maximum_velocity_revolutions_per_second)) ||
        (maximum_acceleration_revolutions_per_second2_q16_16 <= 0) ||
        (maximum_acceleration_revolutions_per_second2_q16_16 >
         float_to_q16_16(position->position_controller->config.
             maximum_acceleration_revolutions_per_second_squared)) ||
        (current_limit_counts == 0u) ||
        (current_limit_counts >
         position->position_controller->config.maximum_current_counts) ||
        (duration_millis <
         position->position_controller->config.minimum_duration_millis) ||
        (duration_millis >
         position->position_controller->config.maximum_duration_millis))
    {
        return COMMAND_STATUS_INVALID_PAYLOAD;
    }

    product = position->product;
    motor_alignment_get_status(
        position->motor_alignment, &alignment_status);
    current_loop_backend_get_snapshot(&loop);
    if ((position->supervisor->state != APP_STATE_READY) ||
        (position->supervisor->authority != APP_AUTHORITY_NONE) ||
        !*position->bridge_ready || !alignment_status.valid ||
        !encoder_control_ready(
            position->encoder_diagnostics,
            position->angle_tracker,
            *product->encoder_feedback_live,
            *position->estimator_fault_flags) ||
        (fabsf(position->angle_tracker->velocity_revolutions_per_second) >
         position->position_controller->config.
             maximum_start_velocity_revolutions_per_second) ||
        app_supervisor_bridge_authorized(position->supervisor) ||
        product->remote_start_requested ||
        product->remote_stop_requested ||
        product->alignment_start_requested ||
        product->alignment_stop_requested ||
        product->torque_start_requested ||
        product->torque_stop_requested ||
        (product->velocity_commands != NULL &&
         (product->velocity_commands->start_requested ||
          product->velocity_commands->stop_requested)) ||
        position->start_requested || position->stop_requested ||
        alignment_controller_is_active(position->alignment_controller) ||
        aligned_torque_controller_is_active(
            position->aligned_torque_controller) ||
        velocity_controller_is_active(position->velocity_controller) ||
        position_controller_is_active(position->position_controller) ||
        loop.active || (loop.fault_flags != 0u) ||
        ((*position->raw_input_levels & USER_INPUT_BUTTON_RIGHT) == 0u))
    {
        return COMMAND_STATUS_UNAVAILABLE;
    }

    position->requested_displacement_revolutions_q16_16 =
        displacement_revolutions_q16_16;
    position->requested_maximum_velocity_q16_16 =
        maximum_velocity_revolutions_per_second_q16_16;
    position->requested_maximum_acceleration_q16_16 =
        maximum_acceleration_revolutions_per_second2_q16_16;
    position->requested_current_limit_counts = current_limit_counts;
    position->requested_duration_millis = duration_millis;
    position->stop_requested = false;
    position->start_requested = true;
    return COMMAND_STATUS_OK;
}

static command_status_t position_get_status(
    void* context,
    command_position_status_t* status)
{
    position_command_context_t* position = context;
    position_controller_status_t controller_status;
    velocity_controller_status_t velocity_status;
    aligned_torque_status_t torque_status;
    motor_alignment_status_t alignment_status;
    current_loop_backend_snapshot_t loop = {0};
    int32_t current_magnitude;
    uint32_t now;

    if ((position == NULL) || (status == NULL) ||
        (position->supervisor == NULL) ||
        (position->position_controller == NULL) ||
        (position->velocity_controller == NULL) ||
        (position->aligned_torque_controller == NULL) ||
        (position->motor_alignment == NULL))
    {
        return COMMAND_STATUS_INTERNAL_ERROR;
    }

    now = timebase_millis();
    position_controller_get_status(
        position->position_controller, &controller_status);
    velocity_controller_get_status(
        position->velocity_controller, &velocity_status);
    aligned_torque_controller_get_status(
        position->aligned_torque_controller, &torque_status);
    motor_alignment_get_status(
        position->motor_alignment, &alignment_status);
    current_loop_backend_get_snapshot(&loop);
    memset(status, 0, sizeof(*status));
    status->schema_version = POSITION_STATUS_SCHEMA_VERSION;
    status->state = (uint8_t)controller_status.state;
    status->result = (uint8_t)controller_status.result;
    status->fault_flags = controller_status.fault_flags;
    status->target_position_revolutions_q16_16 =
        controller_status.target_position_revolutions_q16_16;
    status->reference_position_revolutions_q16_16 =
        controller_status.reference_position_revolutions_q16_16;
    status->measured_position_revolutions_q16_16 =
        controller_status.measured_position_revolutions_q16_16;
    status->reference_velocity_revolutions_per_second_q16_16 =
        controller_status.reference_velocity_revolutions_per_second_q16_16;
    status->target_velocity_revolutions_per_second_q16_16 =
        controller_status.target_velocity_revolutions_per_second_q16_16;
    status->measured_velocity_revolutions_per_second_q16_16 =
        controller_status.measured_velocity_revolutions_per_second_q16_16;
    status->requested_q_current_counts =
        velocity_status.requested_q_current_counts;
    if (controller_status.active)
    {
        status->applied_q_current_counts =
            torque_status.applied_q_current_counts;
    }
    status->current_limit_counts = controller_status.current_limit_counts;
    status->elapsed_millis = controller_status.elapsed_millis;
    if (controller_status.active &&
        ((int32_t)(position->position_controller->deadline_millis - now) > 0))
    {
        status->remaining_millis =
            position->position_controller->deadline_millis - now;
    }
    if (controller_status.active)
    {
        status->flags |= COMMAND_POSITION_FLAG_ACTIVE;
    }
    if (controller_status.active &&
        (position->supervisor->state == APP_STATE_RUN) &&
        (position->supervisor->authority == APP_AUTHORITY_MOTION))
    {
        status->flags |= COMMAND_POSITION_FLAG_AUTHORITY_ACTIVE;
    }
    if (controller_status.active && loop.active)
    {
        status->flags |= COMMAND_POSITION_FLAG_BACKEND_ACTIVE;
    }
    if (alignment_status.valid)
    {
        status->flags |= COMMAND_POSITION_FLAG_ALIGNMENT_VALID;
    }
    if (controller_status.active && velocity_status.active)
    {
        status->flags |= COMMAND_POSITION_FLAG_VELOCITY_ACTIVE;
    }
    if (controller_status.profile_at_target)
    {
        status->flags |= COMMAND_POSITION_FLAG_PROFILE_AT_TARGET;
    }
    if (controller_status.target_settled)
    {
        status->flags |= COMMAND_POSITION_FLAG_TARGET_SETTLED;
    }
    current_magnitude = velocity_status.requested_q_current_counts;
    if (current_magnitude < 0)
    {
        current_magnitude = -current_magnitude;
    }
    if ((controller_status.current_limit_counts != 0u) &&
        (current_magnitude >= controller_status.current_limit_counts))
    {
        status->flags |= COMMAND_POSITION_FLAG_CURRENT_AT_LIMIT;
    }
    status->maximum_relative_travel_revolutions_q16_16 =
        float_to_q16_16(position->position_controller->config.
            maximum_relative_travel_revolutions);
    status->maximum_velocity_revolutions_per_second_q16_16 =
        float_to_q16_16(position->position_controller->config.
            maximum_velocity_revolutions_per_second);
    status->maximum_acceleration_revolutions_per_second2_q16_16 =
        float_to_q16_16(position->position_controller->config.
            maximum_acceleration_revolutions_per_second_squared);
    status->maximum_following_error_revolutions_q16_16 =
        float_to_q16_16(position->position_controller->config.
            maximum_following_error_revolutions);
    return COMMAND_STATUS_OK;
}

static bool build_product_configuration(
    const motor_alignment_t* motor_alignment,
    const phase_current_loop_config_t* current_loop_config,
    product_configuration_t* configuration)
{
    if ((motor_alignment == NULL) || (current_loop_config == NULL) ||
        (configuration == NULL) ||
        !motor_alignment->initialized)
    {
        return false;
    }

    memset(configuration, 0, sizeof(*configuration));
    configuration->encoder_counts_per_revolution =
        motor_alignment->config.encoder_counts_per_revolution;
    configuration->electrical_cycles_per_revolution =
        motor_alignment->config.electrical_cycles_per_revolution;
    motor_alignment_get_status(
        motor_alignment, &configuration->alignment);
    configuration->current_loop_proportional_gain_q16_per_count =
        current_loop_config->proportional_gain_q16_per_count;
    configuration->current_loop_integral_gain_q16_per_count_per_step =
        current_loop_config->integral_gain_q16_per_count_per_step;
    return product_configuration_is_valid(configuration);
}

static bool configuration_write_allowed(
    const product_command_context_t* commissioning)
{
    current_loop_backend_snapshot_t loop = {0};

    if ((commissioning == NULL) ||
        (commissioning->configuration_store == NULL) ||
        (commissioning->motor_alignment == NULL) ||
        (commissioning->alignment_controller == NULL) ||
        (commissioning->aligned_torque_controller == NULL) ||
        (commissioning->rotor_control_runtime == NULL) ||
        (commissioning->supervisor == NULL))
    {
        return false;
    }
    current_loop_backend_get_snapshot(&loop);
    return ((commissioning->supervisor->state == APP_STATE_READY) ||
            (commissioning->supervisor->state == APP_STATE_DIAGNOSTIC)) &&
           (commissioning->supervisor->authority == APP_AUTHORITY_NONE) &&
           !app_supervisor_bridge_authorized(commissioning->supervisor) &&
           loop.initialized && !loop.active && (loop.fault_flags == 0u) &&
           !alignment_controller_is_active(
                commissioning->alignment_controller) &&
           !aligned_torque_controller_is_active(
                commissioning->aligned_torque_controller) &&
           !commissioning->remote_start_requested &&
           !commissioning->remote_stop_requested &&
           !commissioning->alignment_start_requested &&
           !commissioning->alignment_stop_requested &&
           !commissioning->torque_start_requested &&
           !commissioning->torque_stop_requested &&
           !velocity_request_or_control_active(commissioning) &&
           ((commissioning->velocity_commands == NULL) ||
            !commissioning->velocity_commands->stop_requested) &&
           !position_request_or_control_active(commissioning) &&
           ((commissioning->position_commands == NULL) ||
            !commissioning->position_commands->stop_requested);
}

static command_status_t configuration_get_status(
    void* context,
    command_configuration_status_t* status)
{
    product_command_context_t* commissioning = context;
    product_configuration_t active;
    product_configuration_t stored;
    const configuration_store_t* store;

    if ((commissioning == NULL) || (status == NULL) ||
        (commissioning->configuration_store == NULL) ||
        !build_product_configuration(
            commissioning->motor_alignment,
            commissioning->current_loop_config,
            &active))
    {
        return COMMAND_STATUS_INTERNAL_ERROR;
    }

    store = commissioning->configuration_store;
    memset(status, 0, sizeof(*status));
    status->schema_version = 2u;
    status->active_slot = CONFIGURATION_STORE_INVALID_SLOT;
    status->record_schema_version = store->record_valid ?
        store->record_schema_version :
        CONFIGURATION_STORE_RECORD_SCHEMA_VERSION;
    status->active_encoder_counts_per_revolution =
        active.encoder_counts_per_revolution;
    status->active_electrical_cycles_per_revolution =
        active.electrical_cycles_per_revolution;
    status->active_electrical_zero_raw =
        active.alignment.electrical_zero_raw;
    status->active_observed_quarter_step_counts =
        active.alignment.observed_quarter_step_counts;
    status->active_quarter_step_error_counts =
        active.alignment.quarter_step_error_counts;
    status->active_encoder_direction = active.alignment.encoder_direction;
    status->default_current_loop_proportional_gain_q16_per_count =
        PRODUCT_CONFIGURATION_DEFAULT_CURRENT_LOOP_KP_Q16;
    status->default_current_loop_integral_gain_q16_per_count_per_step =
        PRODUCT_CONFIGURATION_DEFAULT_CURRENT_LOOP_KI_Q16;
    status->active_current_loop_proportional_gain_q16_per_count =
        active.current_loop_proportional_gain_q16_per_count;
    status->active_current_loop_integral_gain_q16_per_count_per_step =
        active.current_loop_integral_gain_q16_per_count_per_step;
    status->maximum_current_loop_proportional_gain_q16_per_count =
        PRODUCT_CONFIGURATION_MAXIMUM_CURRENT_LOOP_KP_Q16;
    status->maximum_current_loop_integral_gain_q16_per_count_per_step =
        PRODUCT_CONFIGURATION_MAXIMUM_CURRENT_LOOP_KI_Q16;
    status->flags |= COMMAND_CONFIGURATION_FLAG_WRITE_SUPPORTED;
    if (store->initialized)
    {
        status->flags |= COMMAND_CONFIGURATION_FLAG_STORE_INITIALIZED;
    }
    if (active.alignment.valid)
    {
        status->flags |=
            COMMAND_CONFIGURATION_FLAG_ACTIVE_CALIBRATION_VALID;
    }
    if ((store->valid_slot_mask & 1u) != 0u)
    {
        status->flags |= COMMAND_CONFIGURATION_FLAG_SLOT0_VALID;
    }
    if ((store->valid_slot_mask & 2u) != 0u)
    {
        status->flags |= COMMAND_CONFIGURATION_FLAG_SLOT1_VALID;
    }
    status->last_result = (uint8_t)store->last_result;
    status->active_slot = store->active_slot;
    status->generation = store->generation;
    if (configuration_store_get(store, &stored))
    {
        status->flags |= COMMAND_CONFIGURATION_FLAG_RECORD_VALID;
        if (stored.alignment.valid)
        {
            status->flags |=
                COMMAND_CONFIGURATION_FLAG_STORED_CALIBRATION_VALID;
        }
        if (configuration_store_matches(store, &active))
        {
            status->flags |=
                COMMAND_CONFIGURATION_FLAG_ACTIVE_MATCHES_RECORD;
        }
        status->stored_encoder_counts_per_revolution =
            stored.encoder_counts_per_revolution;
        status->stored_electrical_cycles_per_revolution =
            stored.electrical_cycles_per_revolution;
        status->stored_electrical_zero_raw =
            stored.alignment.electrical_zero_raw;
        status->stored_observed_quarter_step_counts =
            stored.alignment.observed_quarter_step_counts;
        status->stored_quarter_step_error_counts =
            stored.alignment.quarter_step_error_counts;
        status->stored_encoder_direction =
            stored.alignment.encoder_direction;
        status->stored_current_loop_proportional_gain_q16_per_count =
            stored.current_loop_proportional_gain_q16_per_count;
        status->stored_current_loop_integral_gain_q16_per_count_per_step =
            stored.current_loop_integral_gain_q16_per_count_per_step;
    }
    return COMMAND_STATUS_OK;
}

static command_status_t configuration_save_active(void* context)
{
    product_command_context_t* commissioning = context;
    product_configuration_t configuration;

    if ((commissioning == NULL) ||
        !build_product_configuration(
            commissioning->motor_alignment,
            commissioning->current_loop_config,
            &configuration))
    {
        return COMMAND_STATUS_INTERNAL_ERROR;
    }
    if (!configuration_write_allowed(commissioning))
    {
        return COMMAND_STATUS_UNAVAILABLE;
    }

    board_bridge_force_low_zero();
    return configuration_store_save(
               commissioning->configuration_store,
               &configuration) == CONFIGURATION_STORE_RESULT_OK ?
        COMMAND_STATUS_OK : COMMAND_STATUS_INTERNAL_ERROR;
}

static command_status_t configuration_save_alignment_only(void* context)
{
    product_command_context_t* commissioning = context;
    product_configuration_t configuration;

    if ((commissioning == NULL) ||
        !build_product_configuration(
            commissioning->motor_alignment,
            commissioning->current_loop_config,
            &configuration))
    {
        return COMMAND_STATUS_INTERNAL_ERROR;
    }
    if (!configuration_write_allowed(commissioning))
    {
        return COMMAND_STATUS_UNAVAILABLE;
    }

    /* Automatic alignment may persist its newly accepted geometry, but it is
       not user authorization to promote volatile tuning. Merge the previous
       stored gains (or compiled defaults for an empty store) before saving. */
    configuration_store_restore_current_loop_gains(
        commissioning->configuration_store, &configuration);
    board_bridge_force_low_zero();
    return configuration_store_save(
               commissioning->configuration_store,
               &configuration) == CONFIGURATION_STORE_RESULT_OK ?
        COMMAND_STATUS_OK : COMMAND_STATUS_INTERNAL_ERROR;
}

static command_status_t configuration_clear_calibration(void* context)
{
    product_command_context_t* commissioning = context;
    product_configuration_t configuration;

    if ((commissioning == NULL) ||
        !build_product_configuration(
            commissioning->motor_alignment,
            commissioning->current_loop_config,
            &configuration))
    {
        return COMMAND_STATUS_INTERNAL_ERROR;
    }
    if (!configuration_write_allowed(commissioning))
    {
        return COMMAND_STATUS_UNAVAILABLE;
    }

    /* Clearing calibration modifies only motor geometry. Volatile tuning must
       remain active without being promoted as a side effect. */
    configuration_store_restore_current_loop_gains(
        commissioning->configuration_store, &configuration);
    memset(&configuration.alignment, 0, sizeof(configuration.alignment));
    board_bridge_force_low_zero();
    if (configuration_store_save(
            commissioning->configuration_store,
            &configuration) != CONFIGURATION_STORE_RESULT_OK)
    {
        return COMMAND_STATUS_INTERNAL_ERROR;
    }
    if (!rotor_control_runtime_clear_alignment(
            commissioning->rotor_control_runtime))
    {
        return COMMAND_STATUS_INTERNAL_ERROR;
    }
    motor_alignment_clear(commissioning->motor_alignment);
    return COMMAND_STATUS_OK;
}

static command_status_t configuration_apply_current_loop_gains(
    product_command_context_t* commissioning,
    int32_t proportional_gain_q16_per_count,
    int32_t integral_gain_q16_per_count_per_step)
{
    phase_current_loop_config_t candidate;

    if ((commissioning == NULL) ||
        (commissioning->current_loop_config == NULL))
    {
        return COMMAND_STATUS_INTERNAL_ERROR;
    }
    candidate = *commissioning->current_loop_config;
    candidate.proportional_gain_q16_per_count =
        proportional_gain_q16_per_count;
    candidate.integral_gain_q16_per_count_per_step =
        integral_gain_q16_per_count_per_step;
    if ((proportional_gain_q16_per_count < 0) ||
        (proportional_gain_q16_per_count >
         PRODUCT_CONFIGURATION_MAXIMUM_CURRENT_LOOP_KP_Q16) ||
        (integral_gain_q16_per_count_per_step < 0) ||
        (integral_gain_q16_per_count_per_step >
         PRODUCT_CONFIGURATION_MAXIMUM_CURRENT_LOOP_KI_Q16) ||
        !phase_current_loop_config_is_valid(&candidate))
    {
        return COMMAND_STATUS_INVALID_PAYLOAD;
    }
    if (!configuration_write_allowed(commissioning))
    {
        return COMMAND_STATUS_UNAVAILABLE;
    }
    if (!current_loop_backend_reconfigure_gains(
            proportional_gain_q16_per_count,
            integral_gain_q16_per_count_per_step))
    {
        return COMMAND_STATUS_INTERNAL_ERROR;
    }

    *commissioning->current_loop_config = candidate;
    return COMMAND_STATUS_OK;
}

static command_status_t configuration_set_current_loop_gains(
    void* context,
    int32_t proportional_gain_q16_per_count,
    int32_t integral_gain_q16_per_count_per_step)
{
    return configuration_apply_current_loop_gains(
        context,
        proportional_gain_q16_per_count,
        integral_gain_q16_per_count_per_step);
}

static command_status_t configuration_revert_current_loop_gains(
    void* context)
{
    product_command_context_t* commissioning = context;
    product_configuration_t configuration;

    if ((commissioning == NULL) ||
        (commissioning->configuration_store == NULL) ||
        !build_product_configuration(
            commissioning->motor_alignment,
            commissioning->current_loop_config,
            &configuration))
    {
        return COMMAND_STATUS_INTERNAL_ERROR;
    }
    configuration_store_restore_current_loop_gains(
        commissioning->configuration_store, &configuration);
    return configuration_apply_current_loop_gains(
        commissioning,
        configuration.current_loop_proportional_gain_q16_per_count,
        configuration.current_loop_integral_gain_q16_per_count_per_step);
}

static command_status_t commissioning_get_boot_status(
    void* context,
    command_boot_status_t* status)
{
    if ((context == NULL) || (status == NULL))
    {
        return COMMAND_STATUS_INTERNAL_ERROR;
    }

    status->schema_version = 1u;
    status->reset_flags = g_platform_boot_diagnostics.reset_flags;
    status->retained_panic = (uint8_t)g_diagnostics.retained_panic;
    status->uptime_millis = timebase_millis();
    return COMMAND_STATUS_OK;
}

static command_status_t commissioning_get_encoder_status(
    void* context,
    command_encoder_status_t* status)
{
    product_command_context_t* commissioning = context;
    const diagnostics_encoder_t* encoder;
    const angle_tracker_t* angle_tracker;
    motor_alignment_status_t alignment_status;
    uint32_t electrical_phase_q32 = 0u;

    if ((commissioning == NULL) || (status == NULL) ||
        (commissioning->encoder_diagnostics == NULL) ||
        (commissioning->angle_tracker == NULL) ||
        (commissioning->encoder_feedback_live == NULL) ||
        (commissioning->estimator_fault_flags == NULL) ||
        (commissioning->estimator_sample_interval_us == NULL) ||
        (commissioning->estimator_maximum_sample_interval_us == NULL) ||
        (commissioning->motor_alignment == NULL))
    {
        return COMMAND_STATUS_INTERNAL_ERROR;
    }

    encoder = commissioning->encoder_diagnostics;
    angle_tracker = commissioning->angle_tracker;
    memset(status, 0, sizeof(*status));
    status->schema_version = ENCODER_STATUS_SCHEMA_VERSION;
    status->status = (uint8_t)encoder->status;
    status->transport_status = (uint8_t)encoder->transport_status;
    status->angle_raw = (uint16_t)encoder->angle_raw;
    status->flags = (uint8_t)encoder->flags;
    status->sample_count = encoder->sample_count;
    status->error_count = encoder->error_count;
    status->last_attempt_millis = encoder->last_attempt_millis;
    status->estimator_fault_flags =
        *commissioning->estimator_fault_flags;
    status->estimator_sample_interval_us =
        *commissioning->estimator_sample_interval_us;
    status->estimator_maximum_sample_interval_us =
        *commissioning->estimator_maximum_sample_interval_us;
    if (angle_tracker->initialized &&
        (status->estimator_fault_flags == 0u))
    {
        if (*commissioning->encoder_feedback_live)
        {
            status->estimator_flags |= COMMAND_ENCODER_ESTIMATOR_READY;
        }
        status->position_revolutions_q16_16 =
            float_to_q16_16(angle_tracker->position_revolutions);
        status->velocity_revolutions_per_second_q16_16 =
            float_to_q16_16(
                angle_tracker->velocity_revolutions_per_second);
        status->estimator_timestamp_us =
            angle_tracker->last_timestamp_us;
    }

    motor_alignment_get_status(
        commissioning->motor_alignment, &alignment_status);
    status->alignment_zero_raw = alignment_status.electrical_zero_raw;
    status->alignment_direction = alignment_status.encoder_direction;
    if (alignment_status.valid)
    {
        status->estimator_flags |= COMMAND_ENCODER_ALIGNMENT_VALID;
        if (motor_alignment_electrical_phase_q32(
                commissioning->motor_alignment,
                (uint16_t)encoder->angle_raw,
                &electrical_phase_q32))
        {
            status->estimator_flags |=
                COMMAND_ENCODER_ELECTRICAL_PHASE_VALID;
            status->electrical_phase_q32 = electrical_phase_q32;
        }
    }
    return COMMAND_STATUS_OK;
}

static command_status_t commissioning_get_current_trace(
    void* context,
    uint16_t sample_index,
    command_current_trace_sample_t* sample)
{
    current_loop_backend_snapshot_t loop = {0};
    current_loop_backend_trace_sample_t trace;
    uint16_t captured_sample_count;

    if ((context == NULL) || (sample == NULL))
    {
        return COMMAND_STATUS_INTERNAL_ERROR;
    }

    current_loop_backend_get_snapshot(&loop);
    if (loop.active)
    {
        return COMMAND_STATUS_UNAVAILABLE;
    }
    captured_sample_count = current_loop_backend_trace_count();
    if (sample_index >= captured_sample_count)
    {
        return COMMAND_STATUS_INVALID_PAYLOAD;
    }
    if (!current_loop_backend_trace_get(sample_index, &trace))
    {
        return COMMAND_STATUS_INTERNAL_ERROR;
    }

    sample->schema_version = CURRENT_TRACE_SCHEMA_VERSION;
    sample->captured_sample_count = captured_sample_count;
    sample->sample_index = sample_index;
    sample->loop_sample_count = trace.loop_sample_count;
    sample->current_a_reference_counts = trace.current_a_reference_counts;
    sample->current_b_reference_counts = trace.current_b_reference_counts;
    sample->current_a_measured_counts = trace.current_a_measured_counts;
    sample->current_b_measured_counts = trace.current_b_measured_counts;
    sample->phase_a_voltage_permille = trace.phase_a_voltage_permille;
    sample->phase_b_voltage_permille = trace.phase_b_voltage_permille;
    sample->predicted_electrical_phase_q32 =
        trace.predicted_electrical_phase_q32;
    sample->phase_prediction_age_us = trace.phase_prediction_age_us;
    sample->trigger_timer_count = trace.trigger_timer_count;
    sample->trigger_to_dma_timer_ticks =
        trace.trigger_to_dma_timer_ticks;
    sample->dma_to_pwm_stage_cycles = trace.dma_to_pwm_stage_cycles;
    sample->dma_to_trace_record_cycles =
        trace.dma_to_trace_record_cycles;
    sample->pwm_preload_margin_ticks = trace.pwm_preload_margin_ticks;
    return COMMAND_STATUS_OK;
}

static command_status_t commissioning_arm_current_trace(void* context)
{
    if (context == NULL)
    {
        return COMMAND_STATUS_INTERNAL_ERROR;
    }
    return current_loop_backend_trace_arm() ?
        COMMAND_STATUS_OK : COMMAND_STATUS_UNAVAILABLE;
}

static command_status_t commissioning_get_runtime_profile(
    void* context,
    command_runtime_profile_t* profile)
{
    runtime_profile_snapshot_t snapshot;
    size_t index;

    if ((context == NULL) || (profile == NULL))
    {
        return COMMAND_STATUS_INTERNAL_ERROR;
    }
    if (!runtime_profile_get_snapshot(&snapshot))
    {
        return COMMAND_STATUS_UNAVAILABLE;
    }

    memset(profile, 0, sizeof(*profile));
    profile->schema_version = snapshot.schema_version;
    profile->state = snapshot.state;
    profile->captured_release_count = snapshot.captured_release_count;
    profile->incomplete_release_count = snapshot.incomplete_release_count;
    profile->foreground_sample_count = snapshot.foreground_sample_count;
    profile->current_loop_completion_count =
        snapshot.current_loop_completion_count;
    profile->maximum_current_loop_completions_per_release =
        snapshot.maximum_current_loop_completions_per_release;
    for (index = 0u; index < RUNTIME_PROFILE_METRIC_COUNT; ++index)
    {
        profile->metrics[index].total_cycles =
            snapshot.metrics[index].total_cycles;
        profile->metrics[index].maximum_cycles =
            snapshot.metrics[index].maximum_cycles;
    }
    return COMMAND_STATUS_OK;
}

static command_status_t commissioning_arm_runtime_profile(void* context)
{
    if (context == NULL)
    {
        return COMMAND_STATUS_INTERNAL_ERROR;
    }
    if (!cycle_counter_init())
    {
        return COMMAND_STATUS_UNAVAILABLE;
    }
    return runtime_profile_arm() ?
        COMMAND_STATUS_OK : COMMAND_STATUS_UNAVAILABLE;
}

static void wait_milliseconds(uint32_t duration)
{
    const uint32_t start = timebase_millis();

    while ((uint32_t)(timebase_millis() - start) < duration)
    {
        __WFI();
    }
}

static bool display_initialize(i2c_bus_t* bus)
{
    enum
    {
        DISPLAY_RESET_LOW_MS = 1u,
        DISPLAY_RESET_RECOVERY_MS = 10u
    };
    bool i2c_ready;

    if (bus == NULL)
    {
        return false;
    }

    board_display_reset_assert();
    i2c_ready = i2c1_init(platform_apb1_clock_hz());
    wait_milliseconds(DISPLAY_RESET_LOW_MS);
    board_display_reset_release();
    wait_milliseconds(DISPLAY_RESET_RECOVERY_MS);

    if (!i2c_ready)
    {
        return false;
    }

    *bus = i2c1_bus();
    memset(s_display_frame, 0, sizeof(s_display_frame));
    if (ssd1306_initialize(
            bus,
            &SSD1306_PANEL_SERVO57D) != I2C_STATUS_OK)
    {
        return false;
    }

    return ssd1306_write_frame(
               bus,
               &SSD1306_PANEL_SERVO57D,
               s_display_frame,
               sizeof(s_display_frame)) == I2C_STATUS_OK;
}

static void update_rs485_diagnostics(diagnostics_rs485_t* diagnostics)
{
    rs485_stats_t stats;

    rs485_get_stats(&stats);
    diagnostics->status = stats.status;
    diagnostics->rx_bytes = stats.rx_bytes;
    diagnostics->rx_idle_events = stats.rx_idle_events;
    diagnostics->rx_error_count = stats.rx_error_count;
    diagnostics->rx_overrun_count = stats.rx_overrun_count;
    diagnostics->rx_dropped_bytes = stats.rx_dropped_bytes;
    diagnostics->tx_bytes = stats.tx_bytes;
    diagnostics->tx_frame_count = stats.tx_frame_count;
    diagnostics->tx_error_count = stats.tx_error_count;
    diagnostics->tx_busy = stats.tx_busy;
}

static bool native_protocol_transmit(void* context,
                                     const uint8_t* bytes,
                                     size_t length)
{
    (void)context;
    return rs485_write(bytes, length) == RS485_STATUS_OK;
}

static void update_protocol_diagnostics(
    const native_protocol_server_t* server,
    diagnostics_protocol_t* diagnostics)
{
    native_protocol_stats_t stats;

    native_protocol_server_get_stats(server, &stats);
    diagnostics->bytes_consumed = stats.bytes_consumed;
    diagnostics->valid_frames = stats.valid_frames;
    diagnostics->responses_sent = stats.responses_sent;
    diagnostics->cobs_errors = stats.cobs_errors;
    diagnostics->length_errors = stats.length_errors;
    diagnostics->crc_errors = stats.crc_errors;
    diagnostics->version_errors = stats.version_errors;
    diagnostics->ignored_addresses = stats.ignored_addresses;
    diagnostics->broadcasts_dropped = stats.broadcasts_dropped;
    diagnostics->unexpected_message_types =
        stats.unexpected_message_types;
    diagnostics->transmit_rejections = stats.transmit_rejections;
}

static uint32_t signed_i16_as_u32(int16_t value)
{
    return (uint32_t)(int32_t)value;
}

static uint16_t current_loop_fault_display_code(uint32_t fault_flags)
{
    uint16_t bit_position = 1u;

    if (fault_flags == 0u)
    {
        return 0u;
    }
    while ((fault_flags & 1u) == 0u)
    {
        fault_flags >>= 1u;
        ++bit_position;
    }
    return bit_position;
}

static void update_current_loop_diagnostics(
    const current_loop_backend_snapshot_t* snapshot,
    diagnostics_current_loop_t* diagnostics)
{
    diagnostics->ready = snapshot->initialized ? 1u : 0u;
    diagnostics->active = snapshot->active ? 1u : 0u;
    diagnostics->fault_flags = snapshot->fault_flags;
    diagnostics->sample_count = snapshot->sample_count;
    diagnostics->current_a_reference_counts = signed_i16_as_u32(
        snapshot->current_a_reference_counts);
    diagnostics->current_b_reference_counts = signed_i16_as_u32(
        snapshot->current_b_reference_counts);
    diagnostics->current_a_measured_counts = signed_i16_as_u32(
        snapshot->latest_output.current_a_measured_counts);
    diagnostics->current_b_measured_counts = signed_i16_as_u32(
        snapshot->latest_output.current_b_measured_counts);
    diagnostics->phase_a_voltage_permille = signed_i16_as_u32(
        snapshot->latest_output.phase_a_voltage_permille);
    diagnostics->phase_b_voltage_permille = signed_i16_as_u32(
        snapshot->latest_output.phase_b_voltage_permille);
    diagnostics->duty_a1_permille =
        snapshot->latest_output.duty_permille[0];
    diagnostics->duty_a2_permille =
        snapshot->latest_output.duty_permille[1];
    diagnostics->duty_b1_permille =
        snapshot->latest_output.duty_permille[2];
    diagnostics->duty_b2_permille =
        snapshot->latest_output.duty_permille[3];
}

int main(void)
{
    enum
    {
        ENCODER_POWER_UP_DELAY_MS = 20u,
        ENCODER_READ_BURST_BYTES = 4u,
        ENCODER_READ_ANGLE_COMMAND = 0x83u,
        ENCODER_MAXIMUM_SAMPLE_INTERVAL_US = 20000u,
        /*
         * Independent foreground evidence that accepted encoder production is
         * still advancing. The active controllers reject feedback older than
         * 2 ms; one additional 1 ms acquisition/snapshot period lets the
         * foreground observe that deadline without weakening it.
         */
        ENCODER_PROGRESS_TIMEOUT_US = 3000u,
        ENCODER_COUNTS_PER_REVOLUTION = 16384u,
        MOTOR_ELECTRICAL_CYCLES_PER_REVOLUTION = 50u,
        /*
         * Initial alignment-policy candidates, not hardware or motor limits.
         * The 50-count floor comes from the repeatable 303 mA cardinal test;
         * the 165-count ceiling is the current backend's existing qualified
         * request contract. Tune the observation tolerances from bench data.
         */
        ALIGNMENT_MAXIMUM_QUARTER_STEP_ERROR_COUNTS = 12u,
        ALIGNMENT_SETTLE_DURATION_MS = 750u,
        ALIGNMENT_SAMPLE_DURATION_MS = 100u,
        ALIGNMENT_MAXIMUM_DURATION_MS = 4000u,
        ALIGNMENT_MINIMUM_SAMPLE_COUNT = 64u,
        ALIGNMENT_MAXIMUM_SAMPLE_SPAN_COUNTS = 8u,
        ALIGNMENT_MAXIMUM_CLOSURE_ERROR_COUNTS = 12u,
        ALIGNMENT_MAXIMUM_CURRENT_ERROR_COUNTS = 8u,
        ADC_SNAPSHOT_PERIOD_MS = 10u,
        INPUT_SAMPLE_PERIOD_MS = 10u,
        /* Bound safety-service latency without following every ADC wakeup. */
        FOREGROUND_SAFETY_PERIOD_MS = 1u,
        DISPLAY_REFRESH_PERIOD_MS = 200u,
        RS485_FOREGROUND_DRAIN_BYTES = 64u,
        /*
         * Rated-envelope evaluation point for the attached 3 A motor:
         * 495 counts is 2.999 A nominal. The 600-count raw trip is 3.635 A,
         * retaining more than 20 percent independent fault margin. These are
         * evaluation limits, not the power stage's final capability rating.
         */
        CURRENT_LOOP_REFERENCE_LIMIT_COUNTS = 495u,
        CURRENT_LOOP_HARD_LIMIT_COUNTS = 600u,
        CURRENT_LOOP_PHASE_VOLTAGE_LIMIT_PERMILLE = 700u,
        CURRENT_LOOP_DUTY_MARGIN_PERMILLE = 200u,
        /*
         * Initial production torque policy, selected from measured behavior.
         * 495 counts matches the attached motor's reported 3 A rating and
         * opens a deliberate evaluation envelope above the 757 mA
         * bench-proven point. The slew reaches 303 mA in 50 ms, well outside
         * the measured 6.5 ms current-loop rise, while velocity, acceleration,
         * feedback age, and duration remain independent shutdown contracts.
         * This is an evaluation point, not a final product capability claim.
         */
        ALIGNED_TORQUE_MAXIMUM_CURRENT_COUNTS = 495u,
        ALIGNED_TORQUE_MAXIMUM_CURRENT_SLEW_COUNTS_PER_SECOND = 10000u,
        /* The estimator is now the observed-speed boundary for motion. */
        ALIGNED_TORQUE_MAXIMUM_VELOCITY_Q16_16 = 20u << 16,
        /*
         * Independent estimator-plausibility shutdown above the approximately
         * 5,350 rev/s^2 largest nominal-cadence velocity change the 4 kHz
         * filtered estimator can publish while accepting raw motion at its
         * existing 20 rev/s boundary. Hardware is protected independently by
         * current, voltage, duty, speed, timing, and fault contracts.
         */
        ALIGNED_TORQUE_MAXIMUM_ACCELERATION_Q16_16 = 8192u << 16,
        ALIGNED_TORQUE_MAXIMUM_FEEDBACK_INTERVAL_US = 2000u,
        /*
         * The 80%-carrier trigger completes DMA near 45 us. The measured
         * control path stages its preload after the 50 us update, so the
         * command becomes active at the following 100 us update. Predict from
         * DMA completion to that measured application boundary.
         */
        CURRENT_LOOP_PHASE_PREDICTION_OUTPUT_LEAD_US = 55u,
        /*
         * Prediction is allowed one millisecond, or four nominal encoder
         * periods, of dispatch margin beyond the controllers' timestamp-to-
         * timestamp feedback interval, but never beyond the independent
         * total-production guard.
         */
        CURRENT_LOOP_PHASE_PREDICTION_MAXIMUM_AGE_US = 3000u,
        /*
         * Three milliseconds allows one reference update before the deadline
         * even when the first accepted feedback sample arrives at the full
         * two-millisecond feedback-age limit.
         */
        ALIGNED_TORQUE_MINIMUM_DURATION_MS = 3u,
        /*
         * The requested duration is the operation's explicit finite deadline,
         * not a proxy for current or thermal capability. Keep it within one
         * signed half-range so the wrap-safe deadline comparisons are
         * unambiguous for every accepted start time.
         */
        ALIGNED_TORQUE_MAXIMUM_DURATION_MS = INT32_MAX,
        /*
         * Evaluation permission is intentionally ahead of validated motion.
         * Sixteen rev/s leaves 20 percent transient headroom below the
         * estimator/observed-speed boundary. The inner target accepts one
         * additional rev/s so the position correction can use its complete
         * Kp * maximum-following-error budget without clipping at profile
         * speed.
         */
        VELOCITY_MAXIMUM_COMMAND_Q16_16 = 16u << 16,
        VELOCITY_MAXIMUM_TARGET_Q16_16 = 17u << 16,
        VELOCITY_DEFAULT_COMMAND_ACCELERATION_Q16_16 = 16u << 16,
        VELOCITY_MAXIMUM_ACCELERATION_Q16_16 = 256u << 16,
        VELOCITY_MAXIMUM_CURRENT_COUNTS = 495u,
        VELOCITY_MAXIMUM_FEEDBACK_INTERVAL_US = 2000u,
        VELOCITY_MINIMUM_DURATION_MS = 3u,
        VELOCITY_MAXIMUM_DURATION_MS = INT32_MAX,
        POSITION_MAXIMUM_RELATIVE_TRAVEL_Q16_16 = 100u << 16,
        POSITION_MAXIMUM_VELOCITY_Q16_16 = 16u << 16,
        POSITION_MAXIMUM_VELOCITY_TARGET_Q16_16 = 17u << 16,
        POSITION_MAXIMUM_ACCELERATION_Q16_16 = 64u << 16,
        POSITION_MAXIMUM_CURRENT_COUNTS = 495u,
        /* Preserve the established 50 ms settling duration at 4 kHz. */
        POSITION_REQUIRED_SETTLE_SAMPLES = 200u,
        POSITION_MAXIMUM_FEEDBACK_INTERVAL_US = 2000u,
        POSITION_MINIMUM_DURATION_MS = 100u,
        POSITION_MAXIMUM_DURATION_MS = INT32_MAX,
        CURRENT_TEST_AMPLITUDE_COUNTS = 25u,
        CURRENT_TEST_FREQUENCY_MILLIHZ = 500u
    };
    _Static_assert(CURRENT_TEST_AMPLITUDE_COUNTS <
                       CURRENT_LOOP_REFERENCE_LIMIT_COUNTS,
                   "commissioning demand must remain below its reference limit");
    _Static_assert(CURRENT_LOOP_REFERENCE_LIMIT_COUNTS <
                       CURRENT_LOOP_HARD_LIMIT_COUNTS,
                   "requested current must remain below the raw trip");
    _Static_assert((CURRENT_LOOP_HARD_LIMIT_COUNTS * 5u) >=
                       (CURRENT_LOOP_REFERENCE_LIMIT_COUNTS * 6u),
                   "raw trip must remain at least 20 percent above demand");
    _Static_assert(CURRENT_LOOP_PHASE_VOLTAGE_LIMIT_PERMILLE <=
                       (1000u -
                        CURRENT_LOOP_DUTY_MARGIN_PERMILLE),
                   "phase voltage limit violates the active duty margin");
    _Static_assert((CURRENT_LOOP_PHASE_VOLTAGE_LIMIT_PERMILLE + 100u) <=
                       TIM2_CURRENT_TRIGGER_PHASE_PERMILLE,
                   "phase voltage must leave a 5 us ADC quiet interval");
    _Static_assert(CURRENT_LOOP_PHASE_PREDICTION_OUTPUT_LEAD_US <
                       (2u * (1000000u /
                              TIM3_BRIDGE_PWM_FREQUENCY_HZ)),
                   "phase-prediction lead must stay within two carriers");
    _Static_assert(ALIGNED_TORQUE_MAXIMUM_FEEDBACK_INTERVAL_US <
                       CURRENT_LOOP_PHASE_PREDICTION_MAXIMUM_AGE_US,
                   "phase prediction requires dispatch-age headroom");
    _Static_assert(CURRENT_LOOP_PHASE_PREDICTION_MAXIMUM_AGE_US <=
                       ENCODER_PROGRESS_TIMEOUT_US,
                   "phase prediction may not outlive encoder production");
    _Static_assert(CURRENT_LOOP_PHASE_PREDICTION_MAXIMUM_AGE_US <=
                       UINT16_MAX,
                   "reported phase-prediction age limit exceeds the wire field");
    _Static_assert(ALIGNED_TORQUE_MAXIMUM_CURRENT_COUNTS <=
                       CURRENT_LOOP_REFERENCE_LIMIT_COUNTS,
                    "torque policy exceeds the current backend contract");
    _Static_assert(VELOCITY_MAXIMUM_CURRENT_COUNTS <=
                       ALIGNED_TORQUE_MAXIMUM_CURRENT_COUNTS,
                   "velocity current exceeds the aligned actuator contract");
    _Static_assert(POSITION_MAXIMUM_CURRENT_COUNTS <=
                       VELOCITY_MAXIMUM_CURRENT_COUNTS,
                   "position current exceeds the velocity contract");
    _Static_assert(POSITION_MAXIMUM_VELOCITY_Q16_16 <=
                       VELOCITY_MAXIMUM_COMMAND_Q16_16,
                   "position profile exceeds the commandable velocity contract");
    _Static_assert(VELOCITY_MAXIMUM_COMMAND_Q16_16 <
                       VELOCITY_MAXIMUM_TARGET_Q16_16,
                   "velocity correction requires target headroom");
    _Static_assert(POSITION_MAXIMUM_VELOCITY_TARGET_Q16_16 <=
                       VELOCITY_MAXIMUM_TARGET_Q16_16,
                   "position correction exceeds the inner velocity contract");
    _Static_assert((POSITION_MAXIMUM_VELOCITY_TARGET_Q16_16 -
                    POSITION_MAXIMUM_VELOCITY_Q16_16) == (1u << 16),
                   "position correction requires the full Kp-error budget");
    _Static_assert(POSITION_MAXIMUM_ACCELERATION_Q16_16 * 4u <=
                       VELOCITY_MAXIMUM_ACCELERATION_Q16_16,
                   "inner velocity slew requires fourfold profile headroom");
    _Static_assert(VELOCITY_DEFAULT_COMMAND_ACCELERATION_Q16_16 <=
                       VELOCITY_MAXIMUM_ACCELERATION_Q16_16,
                   "default velocity acceleration exceeds its contract");
    _Static_assert(VELOCITY_MAXIMUM_ACCELERATION_Q16_16 * 4u <=
                       ALIGNED_TORQUE_MAXIMUM_ACCELERATION_Q16_16,
                   "observed acceleration shutdown lacks slew headroom");
    _Static_assert(ALIGNED_TORQUE_MAXIMUM_ACCELERATION_Q16_16 <= INT32_MAX,
                   "observed acceleration shutdown exceeds Q16.16 range");
    _Static_assert(POSITION_MINIMUM_DURATION_MS >=
                       VELOCITY_MINIMUM_DURATION_MS,
                   "position duration is unsupported by the velocity layer");
    _Static_assert(POSITION_MAXIMUM_DURATION_MS <=
                       VELOCITY_MAXIMUM_DURATION_MS,
                   "position duration exceeds the velocity layer contract");
    _Static_assert(POSITION_MINIMUM_DURATION_MS >=
                       ALIGNED_TORQUE_MINIMUM_DURATION_MS,
                   "position duration is unsupported by the torque layer");
    _Static_assert(POSITION_MAXIMUM_DURATION_MS <=
                       ALIGNED_TORQUE_MAXIMUM_DURATION_MS,
                   "position duration exceeds the torque layer contract");
    _Static_assert(ENCODER_PROGRESS_TIMEOUT_US >
                       ALIGNED_TORQUE_MAXIMUM_FEEDBACK_INTERVAL_US,
                   "encoder progress guard must allow snapshot observation");
    _Static_assert(ENCODER_PROGRESS_TIMEOUT_US >
                       VELOCITY_MAXIMUM_FEEDBACK_INTERVAL_US,
                   "encoder progress guard must allow velocity feedback");
    _Static_assert(ENCODER_PROGRESS_TIMEOUT_US >
                       POSITION_MAXIMUM_FEEDBACK_INTERVAL_US,
                   "encoder progress guard must allow position feedback");
    _Static_assert(ENCODER_PROGRESS_TIMEOUT_US <=
                       ENCODER_MAXIMUM_SAMPLE_INTERVAL_US,
                   "encoder progress guard exceeds estimator input contract");
    app_supervisor_t drive_supervisor;
    angle_tracker_t angle_tracker;
    encoder_liveness_monitor_t encoder_liveness;
    const angle_tracker_config_t angle_tracker_config = {
        .counts_per_revolution = ENCODER_COUNTS_PER_REVOLUTION,
        .maximum_sample_interval_us =
            ENCODER_MAXIMUM_SAMPLE_INTERVAL_US,
        .maximum_velocity_revolutions_per_second = 20.0f,
        /* Preserve the 1 kHz alpha=0.125 filter pole at the 4 kHz release. */
        .velocity_filter_alpha = 0.03283179f,
    };
    motor_alignment_t motor_alignment;
    configuration_store_backend_t configuration_backend;
    configuration_store_t configuration_store;
    product_configuration_t stored_configuration;
    const motor_alignment_config_t motor_alignment_config = {
        .encoder_counts_per_revolution =
            ENCODER_COUNTS_PER_REVOLUTION,
        .electrical_cycles_per_revolution =
            MOTOR_ELECTRICAL_CYCLES_PER_REVOLUTION,
        .maximum_quarter_step_error_counts =
            ALIGNMENT_MAXIMUM_QUARTER_STEP_ERROR_COUNTS,
    };
    alignment_controller_t alignment_controller;
    const alignment_controller_config_t alignment_controller_config = {
        .settle_duration_millis = ALIGNMENT_SETTLE_DURATION_MS,
        .sample_duration_millis = ALIGNMENT_SAMPLE_DURATION_MS,
        .maximum_duration_millis = ALIGNMENT_MAXIMUM_DURATION_MS,
        .minimum_sample_count = ALIGNMENT_MINIMUM_SAMPLE_COUNT,
        .maximum_sample_span_counts =
            ALIGNMENT_MAXIMUM_SAMPLE_SPAN_COUNTS,
        .maximum_closure_error_counts =
            ALIGNMENT_MAXIMUM_CLOSURE_ERROR_COUNTS,
        .maximum_current_error_counts =
            ALIGNMENT_MAXIMUM_CURRENT_ERROR_COUNTS,
    };
    aligned_torque_controller_t aligned_torque_controller;
    velocity_controller_t velocity_controller;
    position_controller_t position_controller;
    const uint8_t encoder_request[ENCODER_READ_BURST_BYTES] = {
        ENCODER_READ_ANGLE_COMMAND, 0u, 0u, 0u,
    };
    const aligned_torque_config_t aligned_torque_config = {
        .maximum_current_counts =
            ALIGNED_TORQUE_MAXIMUM_CURRENT_COUNTS,
        .maximum_current_slew_counts_per_second =
            ALIGNED_TORQUE_MAXIMUM_CURRENT_SLEW_COUNTS_PER_SECOND,
        .maximum_velocity_revolutions_per_second_q16_16 =
            ALIGNED_TORQUE_MAXIMUM_VELOCITY_Q16_16,
        .maximum_acceleration_revolutions_per_second2_q16_16 =
            ALIGNED_TORQUE_MAXIMUM_ACCELERATION_Q16_16,
        .maximum_feedback_interval_us =
            ALIGNED_TORQUE_MAXIMUM_FEEDBACK_INTERVAL_US,
        .minimum_duration_millis = ALIGNED_TORQUE_MINIMUM_DURATION_MS,
        .maximum_duration_millis = ALIGNED_TORQUE_MAXIMUM_DURATION_MS,
    };
    const velocity_controller_config_t velocity_controller_config = {
        .current_controller = {
            .proportional_gain = 100.0f,
            .integral_gain_per_second = 200.0f,
            .output_limit = (float)VELOCITY_MAXIMUM_CURRENT_COUNTS,
            .integrator_limit = (float)VELOCITY_MAXIMUM_CURRENT_COUNTS,
        },
        .maximum_target_velocity_revolutions_per_second =
            (float)VELOCITY_MAXIMUM_TARGET_Q16_16 / 65536.0f,
        .maximum_target_acceleration_revolutions_per_second_squared =
            (float)VELOCITY_MAXIMUM_ACCELERATION_Q16_16 / 65536.0f,
        .maximum_feedback_velocity_revolutions_per_second = 20.0f,
        .maximum_current_counts = VELOCITY_MAXIMUM_CURRENT_COUNTS,
        .maximum_feedback_interval_us =
            VELOCITY_MAXIMUM_FEEDBACK_INTERVAL_US,
        .minimum_duration_millis = VELOCITY_MINIMUM_DURATION_MS,
        .maximum_duration_millis = VELOCITY_MAXIMUM_DURATION_MS,
    };
    const position_controller_config_t position_controller_config = {
        .maximum_relative_travel_revolutions =
            (float)POSITION_MAXIMUM_RELATIVE_TRAVEL_Q16_16 / 65536.0f,
        .maximum_velocity_revolutions_per_second =
            (float)POSITION_MAXIMUM_VELOCITY_Q16_16 / 65536.0f,
        .maximum_velocity_target_revolutions_per_second =
            (float)POSITION_MAXIMUM_VELOCITY_TARGET_Q16_16 / 65536.0f,
        .maximum_acceleration_revolutions_per_second_squared =
            (float)POSITION_MAXIMUM_ACCELERATION_Q16_16 / 65536.0f,
        .maximum_feedback_velocity_revolutions_per_second = 20.0f,
        .maximum_start_velocity_revolutions_per_second = 0.1f,
        .maximum_following_error_revolutions = 0.25f,
        .position_gain_per_second = 4.0f,
        .position_tolerance_revolutions = 0.002f,
        .velocity_tolerance_revolutions_per_second = 0.02f,
        .maximum_current_counts = POSITION_MAXIMUM_CURRENT_COUNTS,
        .required_settle_samples = POSITION_REQUIRED_SETTLE_SAMPLES,
        .maximum_feedback_interval_us =
            POSITION_MAXIMUM_FEEDBACK_INTERVAL_US,
        .minimum_duration_millis = POSITION_MINIMUM_DURATION_MS,
        .maximum_duration_millis = POSITION_MAXIMUM_DURATION_MS,
    };
    boot_self_test_t self_test;
    diagnostics_encoder_t encoder_diagnostics = {
        .status = MT6816_STATUS_NOT_ATTEMPTED,
        .transport_status = SPI_STATUS_NOT_READY,
    };
    diagnostics_rs485_t rs485_diagnostics = {
        .status = RS485_STATUS_NOT_READY,
    };
    diagnostics_protocol_t protocol_diagnostics = {0};
    diagnostics_current_loop_t current_loop_diagnostics = {0};
    native_protocol_server_t protocol_server;
    uint8_t rs485_receive_buffer[RS485_FOREGROUND_DRAIN_BYTES];
    adc1_current_snapshot_t adc_snapshot = {0};
    adc1_vbus_snapshot_t vbus_snapshot = {0};
    adc_zero_calibrator_t adc_zero_calibrator;
    adc_calibration_t adc_calibration = {0};
    phase_current_loop_config_t current_loop_config = {
        .reference_limit_counts = CURRENT_LOOP_REFERENCE_LIMIT_COUNTS,
        .hard_current_limit_counts = CURRENT_LOOP_HARD_LIMIT_COUNTS,
        .proportional_gain_q16_per_count =
            PRODUCT_CONFIGURATION_DEFAULT_CURRENT_LOOP_KP_Q16,
        .integral_gain_q16_per_count_per_step =
            PRODUCT_CONFIGURATION_DEFAULT_CURRENT_LOOP_KI_Q16,
        .phase_voltage_limit_permille =
            CURRENT_LOOP_PHASE_VOLTAGE_LIMIT_PERMILLE,
        .duty_margin_permille = CURRENT_LOOP_DUTY_MARGIN_PERMILLE,
        .current_a_polarity = 1,
        .current_b_polarity = 1,
    };
    const electrical_phase_predictor_config_t phase_predictor_config = {
        .electrical_cycles_per_mechanical_revolution =
            MOTOR_ELECTRICAL_CYCLES_PER_REVOLUTION,
        .output_lead_us =
            CURRENT_LOOP_PHASE_PREDICTION_OUTPUT_LEAD_US,
        .maximum_prediction_age_us =
            CURRENT_LOOP_PHASE_PREDICTION_MAXIMUM_AGE_US,
        .maximum_mechanical_velocity_q16_16 =
            ALIGNED_TORQUE_MAXIMUM_VELOCITY_Q16_16,
    };
    current_loop_backend_snapshot_t current_loop_snapshot = {0};
    i2c_bus_t display_bus = {0};
    bool display_ready = false;
    bool adc_ready = false;
    bool adc_snapshot_valid = false;
    bool vbus_snapshot_valid = false;
    bool adc_calibration_ready = false;
    bool current_loop_initialized = false;
    uint16_t current_loop_fault_code = 0u;
    int32_t current_a_milliamperes = 0;
    int32_t current_b_milliamperes = 0;
    adc1_status_t adc_status = ADC1_STATUS_NOT_READY;
    bool bridge_ready = false;
    bool encoder_spi_ready = false;
    bool encoder_feedback_live = false;
    uint32_t estimator_fault_flags = 0u;
    uint32_t estimator_sample_interval_us = 0u;
    uint32_t estimator_maximum_sample_interval_us = 0u;
    bool inputs_ready = false;
    bool rs485_ready = false;
    uint32_t heartbeat_count = 0u;
    uint32_t next_heartbeat;
    uint32_t last_encoder_diagnostics_sample_count = 0u;
    uint32_t last_encoder_diagnostics_error_count = 0u;
    uint32_t rotor_active_control_flags = ROTOR_CONTROL_ACTIVE_NONE;
    uint32_t last_rotor_full_snapshot_sequence = 0u;
    uint32_t next_safety_housekeeping;
    uint32_t next_adc_sample;
    uint32_t next_input_sample;
    uint32_t next_display_refresh;
    uint32_t input_levels = USER_INPUT_MASK;
    uint32_t raw_input_levels = USER_INPUT_MASK;
    user_inputs_debouncer_t input_debouncer = {0};
    uint32_t uptime_millis = 0u;
    watchdog_supervisor_t watchdog;
    watchdog_status_t watchdog_status = WATCHDOG_STATUS_NOT_STARTED;
    velocity_command_context_t velocity_commands = {
        .supervisor = &drive_supervisor,
        .bridge_ready = &bridge_ready,
        .encoder_diagnostics = &encoder_diagnostics,
        .angle_tracker = &angle_tracker,
        .estimator_fault_flags = &estimator_fault_flags,
        .motor_alignment = &motor_alignment,
        .alignment_controller = &alignment_controller,
        .aligned_torque_controller = &aligned_torque_controller,
        .velocity_controller = &velocity_controller,
        .rotor_control_runtime = &rotor_control_runtime,
        .raw_input_levels = &raw_input_levels,
        .maximum_command_velocity_revolutions_per_second_q16_16 =
            VELOCITY_MAXIMUM_COMMAND_Q16_16,
        .default_command_acceleration_revolutions_per_second2_q16_16 =
            VELOCITY_DEFAULT_COMMAND_ACCELERATION_Q16_16,
    };
    position_command_context_t position_commands = {
        .supervisor = &drive_supervisor,
        .bridge_ready = &bridge_ready,
        .encoder_diagnostics = &encoder_diagnostics,
        .angle_tracker = &angle_tracker,
        .estimator_fault_flags = &estimator_fault_flags,
        .motor_alignment = &motor_alignment,
        .alignment_controller = &alignment_controller,
        .aligned_torque_controller = &aligned_torque_controller,
        .velocity_controller = &velocity_controller,
        .position_controller = &position_controller,
        .rotor_control_runtime = &rotor_control_runtime,
        .raw_input_levels = &raw_input_levels,
    };
    product_command_context_t commissioning_context = {
        .supervisor = &drive_supervisor,
        .adc_ready = &adc_ready,
        .adc_snapshot_valid = &adc_snapshot_valid,
        .adc_calibration_ready = &adc_calibration_ready,
        .current_loop_initialized = &current_loop_initialized,
        .current_loop_fault_code = &current_loop_fault_code,
        .bridge_ready = &bridge_ready,
        .adc_status = &adc_status,
        .adc_snapshot = &adc_snapshot,
        .vbus_snapshot_valid = &vbus_snapshot_valid,
        .vbus_snapshot = &vbus_snapshot,
        .adc_calibration = &adc_calibration,
        .encoder_diagnostics = &encoder_diagnostics,
        .angle_tracker = &angle_tracker,
        .encoder_feedback_live = &encoder_feedback_live,
        .estimator_fault_flags = &estimator_fault_flags,
        .estimator_sample_interval_us =
            &estimator_sample_interval_us,
        .estimator_maximum_sample_interval_us =
            &estimator_maximum_sample_interval_us,
        .motor_alignment = &motor_alignment,
        .alignment_controller = &alignment_controller,
        .aligned_torque_controller = &aligned_torque_controller,
        .velocity_controller = &velocity_controller,
        .rotor_control_runtime = &rotor_control_runtime,
        .configuration_store = &configuration_store,
        .raw_input_levels = &raw_input_levels,
        .input_levels = &input_levels,
        .current_loop_config = &current_loop_config,
        .maximum_test_amplitude_counts =
            CURRENT_LOOP_REFERENCE_LIMIT_COUNTS,
        .test_amplitude_counts = CURRENT_TEST_AMPLITUDE_COUNTS,
        .test_frequency_millihz = CURRENT_TEST_FREQUENCY_MILLIHZ,
        .test_controller_mode =
            COMMAND_CURRENT_TEST_CONTROLLER_STATIONARY,
        .velocity_commands = &velocity_commands,
        .position_commands = &position_commands,
    };
    velocity_commands.product = &commissioning_context;
    position_commands.product = &commissioning_context;
    const command_service_context_t command_context = {
        .product_id = COMMAND_SERVICE_PRODUCT_ID_MKS57D,
        .firmware_major = MKS57D_FIRMWARE_VERSION_MAJOR,
        .firmware_minor = MKS57D_FIRMWARE_VERSION_MINOR,
        .firmware_patch = MKS57D_FIRMWARE_VERSION_PATCH,
        .protocol_major = NATIVE_PROTOCOL_VERSION_MAJOR,
        .protocol_minor = NATIVE_PROTOCOL_VERSION_MINOR,
        .capabilities = DIAGNOSTICS_CAPABILITIES_CURRENT,
        .commissioning = {
            .context = &commissioning_context,
            .get_status = commissioning_get_status,
            .configure = commissioning_configure,
            .start = commissioning_start,
            .stop = commissioning_stop,
            .get_boot_status = commissioning_get_boot_status,
            .get_encoder_status = commissioning_get_encoder_status,
            .get_current_trace = commissioning_get_current_trace,
            .arm_current_trace = commissioning_arm_current_trace,
            .get_runtime_profile = commissioning_get_runtime_profile,
            .arm_runtime_profile = commissioning_arm_runtime_profile,
        },
        .alignment = {
            .context = &commissioning_context,
            .start = alignment_start,
            .get_status = alignment_get_status,
        },
        .drive = {
            .context = &commissioning_context,
            .stop = commissioning_stop,
            .clear_faults = drive_clear_faults,
        },
        .configuration = {
            .context = &commissioning_context,
            .get_status = configuration_get_status,
            .save = configuration_save_active,
            .clear_calibration = configuration_clear_calibration,
            .set_current_loop_gains =
                configuration_set_current_loop_gains,
            .revert_current_loop_gains =
                configuration_revert_current_loop_gains,
        },
        .aligned_torque = {
            .context = &commissioning_context,
            .start = aligned_torque_start,
            .get_status = aligned_torque_get_status,
        },
        .velocity = {
            .context = &velocity_commands,
            .start = velocity_start,
            .get_status = velocity_get_status,
        },
        .position = {
            .context = &position_commands,
            .start_relative = position_start_relative,
            .get_status = position_get_status,
        },
    };

    if (!platform_early_memory_ready())
    {
        platform_panic(PANIC_EARLY_PLATFORM_INIT);
    }

    if (!app_supervisor_init(&drive_supervisor))
    {
        platform_panic(PANIC_INTERNAL_INVARIANT);
    }
    if (!angle_tracker_init(&angle_tracker, &angle_tracker_config) ||
        !encoder_liveness_monitor_init(
            &encoder_liveness, ENCODER_PROGRESS_TIMEOUT_US) ||
        !motor_alignment_init(
            &motor_alignment, &motor_alignment_config) ||
        !alignment_controller_init(
            &alignment_controller, &alignment_controller_config) ||
        !aligned_torque_controller_init(
            &aligned_torque_controller, &aligned_torque_config) ||
        !velocity_controller_init(
            &velocity_controller, &velocity_controller_config) ||
        !position_controller_init(
            &position_controller, &position_controller_config))
    {
        platform_panic(PANIC_INTERNAL_INVARIANT);
    }
    if (!configuration_flash_backend_init(&configuration_backend))
    {
        platform_panic(PANIC_INTERNAL_INVARIANT);
    }
    (void)configuration_store_init(
        &configuration_store, &configuration_backend);
    if (configuration_store_get(
            &configuration_store, &stored_configuration))
    {
        current_loop_config.proportional_gain_q16_per_count =
            stored_configuration.
                current_loop_proportional_gain_q16_per_count;
        current_loop_config.integral_gain_q16_per_count_per_step =
            stored_configuration.
                current_loop_integral_gain_q16_per_count_per_step;
        if (stored_configuration.alignment.valid &&
            (stored_configuration.encoder_counts_per_revolution ==
             motor_alignment.config.encoder_counts_per_revolution) &&
            (stored_configuration.electrical_cycles_per_revolution ==
             motor_alignment.config.electrical_cycles_per_revolution))
        {
            (void)motor_alignment_restore(
                &motor_alignment, &stored_configuration.alignment);
        }
    }
    if (!rotor_control_runtime_init(
            &rotor_control_runtime,
            &angle_tracker,
            &motor_alignment,
            &alignment_controller,
            &aligned_torque_controller,
            &velocity_controller,
            &position_controller) ||
        !rotor_control_runtime_get_snapshot(
            &rotor_control_runtime, &rotor_control_snapshot) ||
        !rotor_control_runtime_get_progress_snapshot(
            &rotor_control_runtime, &rotor_control_progress_snapshot))
    {
        platform_panic(PANIC_INTERNAL_INVARIANT);
    }

    boot_self_test_init(&self_test, BOOT_SELF_TEST_REQUIRED_PASSIVE);
    boot_self_test_pass(&self_test, BOOT_SELF_TEST_EARLY_MEMORY);
    diagnostics_init((uint32_t)drive_supervisor.state,
                     uptime_millis,
                     heartbeat_count,
                     (uint32_t)watchdog_status,
                     &self_test);

    if (platform_clock_init() != PLATFORM_BOOT_READY)
    {
        boot_self_test_fail(&self_test, BOOT_SELF_TEST_CLOCK);
        diagnostics_publish((uint32_t)drive_supervisor.state,
                            uptime_millis,
                            heartbeat_count,
                            (uint32_t)watchdog_status,
                            &self_test);
        platform_panic(PANIC_CLOCK_INIT);
    }
    boot_self_test_pass(&self_test, BOOT_SELF_TEST_CLOCK);
    diagnostics_publish((uint32_t)drive_supervisor.state,
                        uptime_millis,
                        heartbeat_count,
                        (uint32_t)watchdog_status,
                        &self_test);

    if (!interrupt_priority_init())
    {
        boot_self_test_fail(&self_test, BOOT_SELF_TEST_INTERRUPT_POLICY);
        diagnostics_publish((uint32_t)drive_supervisor.state,
                            uptime_millis,
                            heartbeat_count,
                            (uint32_t)watchdog_status,
                            &self_test);
        platform_panic(PANIC_INTERRUPT_PRIORITY_INIT);
    }
    boot_self_test_pass(&self_test, BOOT_SELF_TEST_INTERRUPT_POLICY);
    diagnostics_publish((uint32_t)drive_supervisor.state,
                        uptime_millis,
                        heartbeat_count,
                        (uint32_t)watchdog_status,
                        &self_test);

    board_init_passive();
    if (!board_passive_invariants_hold())
    {
        boot_self_test_fail(&self_test, BOOT_SELF_TEST_PASSIVE_BOARD);
        diagnostics_publish((uint32_t)drive_supervisor.state,
                            uptime_millis,
                            heartbeat_count,
                            (uint32_t)watchdog_status,
                            &self_test);
        platform_panic(PANIC_PASSIVE_BOARD_INVARIANT);
    }
    boot_self_test_pass(&self_test, BOOT_SELF_TEST_PASSIVE_BOARD);
    diagnostics_publish((uint32_t)drive_supervisor.state,
                        uptime_millis,
                        heartbeat_count,
                        (uint32_t)watchdog_status,
                        &self_test);

    if (!timebase_init())
    {
        boot_self_test_fail(&self_test, BOOT_SELF_TEST_TIMEBASE);
        diagnostics_publish((uint32_t)drive_supervisor.state,
                            uptime_millis,
                            heartbeat_count,
                            (uint32_t)watchdog_status,
                            &self_test);
        platform_panic(PANIC_TIMEBASE_INIT);
    }
    boot_self_test_pass(&self_test, BOOT_SELF_TEST_TIMEBASE);
    uptime_millis = timebase_millis();
    diagnostics_publish((uint32_t)drive_supervisor.state,
                        uptime_millis,
                        heartbeat_count,
                        (uint32_t)watchdog_status,
                        &self_test);

    display_ready = display_initialize(&display_bus);
    adc_status = adc1_init_passive(SystemCoreClock);
    adc_ready = adc_status == ADC1_STATUS_OK;
    if (!adc_zero_calibrator_init(&adc_zero_calibrator,
                                  ADC_NOMINAL_REFERENCE_VOLTS))
    {
        platform_panic(PANIC_INTERNAL_INVARIANT);
    }
    inputs_ready = board_inputs_init();
    if (inputs_ready)
    {
        raw_input_levels = board_inputs_read_raw();
        inputs_ready = user_inputs_debouncer_init(
            &input_debouncer,
            raw_input_levels);
        input_levels = user_inputs_debounced_levels(&input_debouncer);
    }
    if (!inputs_ready)
    {
        platform_panic(PANIC_DRIVE_CONTROL);
    }
    if (adc_ready)
    {
        adc_status = adc1_start_pwm_synchronized_current();
        adc_ready = adc_status == ADC1_STATUS_OK;
    }
    if (adc_ready &&
        !tim2_current_trigger_init(platform_apb1_timer_clock_hz()))
    {
        platform_panic(PANIC_DRIVE_CONTROL);
    }
    if (!board_bridge_pwm_init(
            platform_apb1_timer_clock_hz()))
    {
        platform_panic(PANIC_DRIVE_CONTROL);
    }
    /* Current feedback is proven before bridge authority is restored. */
    bridge_ready = false;

    encoder_spi_ready =
        spi1_init(platform_apb2_clock_hz()) &&
        spi1_periodic_exchange_start(
            encoder_request,
            sizeof(encoder_request),
            platform_apb1_timer_clock_hz(),
            ENCODER_POWER_UP_DELAY_MS,
            rotor_control_runtime_spi_callback,
            &rotor_control_runtime);
    if (!encoder_spi_ready)
    {
        rotor_control_runtime_spi_callback(
            &rotor_control_runtime,
            SPI_STATUS_NOT_READY,
            NULL,
            0u,
            timebase_micros());
        (void)rotor_control_runtime_get_snapshot(
            &rotor_control_runtime, &rotor_control_snapshot);
        (void)rotor_control_runtime_get_progress_snapshot(
            &rotor_control_runtime, &rotor_control_progress_snapshot);
        encoder_diagnostics =
            rotor_control_progress_snapshot.encoder_diagnostics;
    }
    diagnostics_publish_encoder(&encoder_diagnostics);

    rs485_diagnostics.status =
        (uint32_t)rs485_init(platform_apb2_clock_hz());
    rs485_ready =
        rs485_diagnostics.status == (uint32_t)RS485_STATUS_OK;
    if (rs485_ready)
    {
        update_rs485_diagnostics(&rs485_diagnostics);
    }
    else
    {
        rs485_diagnostics.rx_error_count = 1u;
    }
    diagnostics_publish_rs485(&rs485_diagnostics);

    if (!native_protocol_server_init(
            &protocol_server,
            NATIVE_PROTOCOL_DEFAULT_DEVICE_ADDRESS,
            &command_context,
            native_protocol_transmit,
            NULL))
    {
        platform_panic(PANIC_INTERNAL_INVARIANT);
    }
    protocol_diagnostics.ready = 1u;
    update_protocol_diagnostics(&protocol_server, &protocol_diagnostics);
    diagnostics_publish_protocol(&protocol_diagnostics);

    if (!app_supervisor_handle_event(
            &drive_supervisor,
            APP_EVENT_PASSIVE_INIT_COMPLETE,
            (app_transition_context_t){0}))
    {
        platform_panic(PANIC_INTERNAL_INVARIANT);
    }

    if (drive_supervisor.state != APP_STATE_DIAGNOSTIC)
    {
        boot_self_test_fail(&self_test, BOOT_SELF_TEST_APPLICATION_STATE);
        diagnostics_publish((uint32_t)drive_supervisor.state,
                            uptime_millis,
                            heartbeat_count,
                            (uint32_t)watchdog_status,
                            &self_test);
        platform_panic(PANIC_INTERNAL_INVARIANT);
    }
    boot_self_test_pass(&self_test, BOOT_SELF_TEST_APPLICATION_STATE);
    diagnostics_publish((uint32_t)drive_supervisor.state,
                        uptime_millis,
                        heartbeat_count,
                        (uint32_t)watchdog_status,
                        &self_test);

    watchdog_status = watchdog_supervisor_start(&watchdog, timebase_millis());
    if (watchdog_status != WATCHDOG_STATUS_READY)
    {
        boot_self_test_fail(&self_test, BOOT_SELF_TEST_WATCHDOG);
        diagnostics_publish((uint32_t)drive_supervisor.state,
                            timebase_millis(),
                            heartbeat_count,
                            (uint32_t)watchdog_status,
                            &self_test);
        platform_panic(PANIC_WATCHDOG_INIT);
    }
    boot_self_test_pass(&self_test, BOOT_SELF_TEST_WATCHDOG);

    diagnostics_publish((uint32_t)drive_supervisor.state,
                        timebase_millis(),
                        heartbeat_count,
                        (uint32_t)watchdog_status,
                        &self_test);
    next_heartbeat = timebase_millis() + 250u;
    next_safety_housekeeping = timebase_millis();
    next_adc_sample = timebase_millis();
    next_input_sample = timebase_millis();
    next_display_refresh =
        timebase_millis() + ENCODER_POWER_UP_DELAY_MS;

    for (;;)
    {
        bool diagnostics_due = false;
        bool rotor_full_snapshot_due = false;
        const uint32_t now = timebase_millis();
        const bool safety_housekeeping_due =
            (int32_t)(now - next_safety_housekeeping) >= 0;
        const bool runtime_profile_foreground_active =
            safety_housekeeping_due && runtime_profile_is_armed();
        const uint32_t runtime_profile_foreground_start_cycle_count =
            runtime_profile_foreground_active ? cycle_counter_read() : 0u;
        const uint32_t rotor_events =
            safety_housekeeping_due ?
                rotor_control_runtime_take_events(
                    &rotor_control_runtime) :
                ROTOR_CONTROL_EVENT_NONE;
        rotor_full_snapshot_due =
            rotor_events != ROTOR_CONTROL_EVENT_NONE;
        if (safety_housekeeping_due)
        {
            next_safety_housekeeping =
                now + FOREGROUND_SAFETY_PERIOD_MS;
        }
        if (safety_housekeeping_due ||
            (rotor_events != ROTOR_CONTROL_EVENT_NONE))
        {
            if (!rotor_control_runtime_get_progress_snapshot(
                    &rotor_control_runtime,
                    &rotor_control_progress_snapshot))
            {
                board_bridge_force_low_zero();
                platform_panic(PANIC_INTERNAL_INVARIANT);
            }
            encoder_diagnostics =
                rotor_control_progress_snapshot.encoder_diagnostics;
            angle_tracker.position_revolutions =
                rotor_control_progress_snapshot.
                    estimator_position_revolutions;
            angle_tracker.velocity_revolutions_per_second =
                rotor_control_progress_snapshot.
                    estimator_velocity_revolutions_per_second;
            angle_tracker.initialized =
                rotor_control_progress_snapshot.estimator_initialized != 0u;
            angle_tracker.last_timestamp_us =
                rotor_control_progress_snapshot.estimator_timestamp_us;
            estimator_fault_flags =
                rotor_control_progress_snapshot.estimator_fault_flags;
            rotor_active_control_flags =
                rotor_control_progress_snapshot.active_control_flags;
            if (rotor_control_progress_snapshot.full_snapshot_sequence !=
                last_rotor_full_snapshot_sequence)
            {
                /* Pull each decimated or transition-forced full publication. */
                rotor_full_snapshot_due = true;
            }
            if (encoder_diagnostics.sample_count !=
                    last_encoder_diagnostics_sample_count ||
                encoder_diagnostics.error_count !=
                    last_encoder_diagnostics_error_count)
            {
                last_encoder_diagnostics_sample_count =
                    encoder_diagnostics.sample_count;
                last_encoder_diagnostics_error_count =
                    encoder_diagnostics.error_count;
                diagnostics_publish_encoder(&encoder_diagnostics);
                diagnostics_due = true;
            }
            {
                const bool encoder_feedback_was_live =
                    encoder_feedback_live;
                const uint32_t encoder_liveness_now_us =
                    timebase_micros();

                encoder_feedback_live = encoder_liveness_monitor_update(
                    &encoder_liveness,
                    angle_tracker.initialized,
                    encoder_diagnostics.sample_count,
                    angle_tracker.last_timestamp_us,
                    encoder_liveness_now_us);
                if (encoder_feedback_live != encoder_feedback_was_live)
                {
                    diagnostics_due = true;
                }
            }
        }

        if (rotor_full_snapshot_due)
        {
            if (!rotor_control_runtime_get_snapshot(
                    &rotor_control_runtime, &rotor_control_snapshot))
            {
                board_bridge_force_low_zero();
                platform_panic(PANIC_INTERNAL_INVARIANT);
            }
            angle_tracker = rotor_control_snapshot.angle_tracker;
            motor_alignment = rotor_control_snapshot.motor_alignment;
            alignment_controller =
                rotor_control_snapshot.alignment_controller;
            aligned_torque_controller =
                rotor_control_snapshot.torque_controller;
            velocity_controller =
                rotor_control_snapshot.velocity_controller;
            position_controller =
                rotor_control_snapshot.position_controller;
            estimator_sample_interval_us =
                rotor_control_snapshot.estimator_sample_interval_us;
            estimator_maximum_sample_interval_us =
                rotor_control_snapshot.
                    estimator_maximum_sample_interval_us;
            last_rotor_full_snapshot_sequence =
                rotor_control_progress_snapshot.full_snapshot_sequence;
            angle_tracker.position_revolutions =
                rotor_control_progress_snapshot.
                    estimator_position_revolutions;
            angle_tracker.velocity_revolutions_per_second =
                rotor_control_progress_snapshot.
                    estimator_velocity_revolutions_per_second;
            angle_tracker.initialized =
                rotor_control_progress_snapshot.estimator_initialized != 0u;
            angle_tracker.last_timestamp_us =
                rotor_control_progress_snapshot.estimator_timestamp_us;
        }

        if ((rotor_events & ROTOR_CONTROL_EVENT_FAULT) != 0u)
        {
            commissioning_context.remote_authority_active = false;
            commissioning_context.remote_start_requested = false;
            commissioning_context.remote_stop_requested = false;
            commissioning_context.alignment_start_requested = false;
            commissioning_context.alignment_stop_requested = false;
            commissioning_context.torque_start_requested = false;
            commissioning_context.torque_stop_requested = false;
            velocity_commands.start_requested = false;
            velocity_commands.stop_requested = false;
            position_commands.start_requested = false;
            position_commands.stop_requested = false;
            bridge_ready = false;
            (void)app_supervisor_handle_event(
                &drive_supervisor,
                APP_EVENT_FAULT_DETECTED,
                (app_transition_context_t){0});
            board_bridge_force_low_zero();
            diagnostics_due = true;
        }
        else if ((rotor_events &
                  ROTOR_CONTROL_EVENT_ALIGNMENT_COMPLETED) != 0u)
        {
            commissioning_context.alignment_start_requested = false;
            commissioning_context.alignment_stop_requested = false;
            if (!app_supervisor_handle_event(
                    &drive_supervisor,
                    APP_EVENT_ALIGNMENT_COMPLETED,
                    (app_transition_context_t){0}))
            {
                board_bridge_force_low_zero();
                platform_panic(PANIC_INTERNAL_INVARIANT);
            }
            (void)configuration_save_alignment_only(
                &commissioning_context);
            diagnostics_due = true;
        }
        else if ((rotor_events &
                  ROTOR_CONTROL_EVENT_AUTHORITY_RELEASED) != 0u)
        {
            commissioning_context.alignment_start_requested = false;
            commissioning_context.alignment_stop_requested = false;
            commissioning_context.torque_start_requested = false;
            commissioning_context.torque_stop_requested = false;
            velocity_commands.start_requested = false;
            velocity_commands.stop_requested = false;
            position_commands.start_requested = false;
            position_commands.stop_requested = false;
            if (app_supervisor_bridge_authorized(&drive_supervisor) &&
                (drive_supervisor.authority == APP_AUTHORITY_MOTION) &&
                !app_supervisor_handle_event(
                    &drive_supervisor,
                    APP_EVENT_AUTHORITY_RELEASED,
                    (app_transition_context_t){0}))
            {
                board_bridge_force_low_zero();
                platform_panic(PANIC_INTERNAL_INVARIANT);
            }
            diagnostics_due = true;
        }

        raw_input_levels = board_inputs_read_raw();
        if (inputs_ready &&
            ((int32_t)(now - next_input_sample) >= 0))
        {
            (void)user_inputs_debouncer_update(
                &input_debouncer,
                raw_input_levels);
            input_levels =
                user_inputs_debounced_levels(&input_debouncer);
            next_input_sample = now + INPUT_SAMPLE_PERIOD_MS;
        }

        if (rs485_ready)
        {
            const size_t received = rs485_read(
                rs485_receive_buffer,
                sizeof(rs485_receive_buffer));

            if (received != 0u)
            {
                rs485_diagnostics.last_rx_byte =
                    rs485_receive_buffer[received - 1u];
                native_protocol_server_consume(&protocol_server,
                                               rs485_receive_buffer,
                                               received);
                update_protocol_diagnostics(&protocol_server,
                                            &protocol_diagnostics);
                diagnostics_due = true;
            }
            if ((received != 0u) || safety_housekeeping_due)
            {
                update_rs485_diagnostics(&rs485_diagnostics);
                if (rs485_diagnostics.status !=
                    (uint32_t)RS485_STATUS_OK)
                {
                    rs485_ready = false;
                    commissioning_context.remote_stop_requested = true;
                    commissioning_context.alignment_stop_requested = true;
                    commissioning_context.torque_stop_requested = true;
                    velocity_commands.stop_requested = true;
                    position_commands.stop_requested = true;
                    diagnostics_due = true;
                }
            }
        }

        if (((rotor_active_control_flags &
              ROTOR_CONTROL_ACTIVE_ALIGNMENT) != 0u) &&
            ((raw_input_levels & USER_INPUT_BUTTON_RIGHT) == 0u))
        {
            commissioning_context.alignment_stop_requested = true;
        }
        if ((commissioning_context.torque_start_requested ||
             ((rotor_active_control_flags &
               ROTOR_CONTROL_ACTIVE_ALIGNED_TORQUE) != 0u)) &&
            ((raw_input_levels & USER_INPUT_BUTTON_RIGHT) == 0u))
        {
            commissioning_context.torque_stop_requested = true;
        }
        if ((velocity_commands.start_requested ||
             ((rotor_active_control_flags &
               ROTOR_CONTROL_ACTIVE_VELOCITY) != 0u)) &&
            ((rotor_active_control_flags &
              ROTOR_CONTROL_ACTIVE_POSITION) == 0u) &&
            ((raw_input_levels & USER_INPUT_BUTTON_RIGHT) == 0u))
        {
            velocity_commands.stop_requested = true;
        }
        if ((position_commands.start_requested ||
             ((rotor_active_control_flags &
               ROTOR_CONTROL_ACTIVE_POSITION) != 0u)) &&
            ((raw_input_levels & USER_INPUT_BUTTON_RIGHT) == 0u))
        {
            position_commands.stop_requested = true;
        }

        if (commissioning_context.alignment_stop_requested)
        {
            commissioning_context.alignment_start_requested = false;
            commissioning_context.alignment_stop_requested = false;
            rotor_control_runtime_request_stop(
                &rotor_control_runtime);
            diagnostics_due = true;
        }

        if (commissioning_context.alignment_start_requested)
        {
            const app_transition_context_t energize_context = {
                .safe_to_energize =
                    bridge_ready && adc_snapshot_valid &&
                    encoder_control_ready(
                        &encoder_diagnostics,
                        &angle_tracker,
                        encoder_feedback_live,
                        estimator_fault_flags),
            };
            commissioning_context.alignment_start_requested = false;
            if (!app_supervisor_handle_event(
                    &drive_supervisor,
                    APP_EVENT_ALIGNMENT_REQUESTED,
                    energize_context) ||
                !rotor_control_runtime_request_alignment(
                    &rotor_control_runtime,
                    commissioning_context.alignment_current_counts))
            {
                bridge_ready = false;
                (void)app_supervisor_handle_event(
                    &drive_supervisor,
                    APP_EVENT_FAULT_DETECTED,
                    (app_transition_context_t){0});
                board_bridge_force_low_zero();
            }
            diagnostics_due = true;
        }

        if (commissioning_context.torque_stop_requested ||
            velocity_commands.stop_requested ||
            position_commands.stop_requested)
        {
            commissioning_context.torque_start_requested = false;
            commissioning_context.torque_stop_requested = false;
            velocity_commands.start_requested = false;
            velocity_commands.stop_requested = false;
            position_commands.start_requested = false;
            position_commands.stop_requested = false;
            rotor_control_runtime_request_stop(
                &rotor_control_runtime);
            diagnostics_due = true;
        }

        if (commissioning_context.torque_start_requested)
        {
            const app_transition_context_t energize_context = {
                .safe_to_energize =
                    bridge_ready && adc_snapshot_valid &&
                    encoder_control_ready(
                        &encoder_diagnostics,
                        &angle_tracker,
                        encoder_feedback_live,
                        estimator_fault_flags),
            };

            commissioning_context.torque_start_requested = false;
            if (!app_supervisor_handle_event(
                    &drive_supervisor,
                    APP_EVENT_MOTION_RUN_REQUESTED,
                    energize_context) ||
                !rotor_control_runtime_request_torque(
                    &rotor_control_runtime,
                    commissioning_context.torque_q_current_counts,
                    commissioning_context.torque_duration_millis))
            {
                bridge_ready = false;
                (void)app_supervisor_handle_event(
                    &drive_supervisor,
                    APP_EVENT_FAULT_DETECTED,
                    (app_transition_context_t){0});
                board_bridge_force_low_zero();
            }
            diagnostics_due = true;
        }

        if (velocity_commands.start_requested)
        {
            const app_transition_context_t energize_context = {
                .safe_to_energize =
                    bridge_ready && adc_snapshot_valid &&
                    encoder_control_ready(
                        &encoder_diagnostics,
                        &angle_tracker,
                        encoder_feedback_live,
                        estimator_fault_flags),
            };

            velocity_commands.start_requested = false;
            if (!app_supervisor_handle_event(
                    &drive_supervisor,
                    APP_EVENT_MOTION_RUN_REQUESTED,
                    energize_context) ||
                !rotor_control_runtime_request_velocity(
                    &rotor_control_runtime,
                    velocity_commands.
                        requested_velocity_revolutions_per_second_q16_16,
                    velocity_commands.
                        requested_acceleration_revolutions_per_second2_q16_16,
                    velocity_commands.requested_current_limit_counts,
                    velocity_commands.requested_duration_millis))
            {
                bridge_ready = false;
                (void)app_supervisor_handle_event(
                    &drive_supervisor,
                    APP_EVENT_FAULT_DETECTED,
                    (app_transition_context_t){0});
                board_bridge_force_low_zero();
            }
            diagnostics_due = true;
        }

        if (position_commands.start_requested)
        {
            const app_transition_context_t energize_context = {
                .safe_to_energize =
                    bridge_ready && adc_snapshot_valid &&
                    encoder_control_ready(
                        &encoder_diagnostics,
                        &angle_tracker,
                        encoder_feedback_live,
                        estimator_fault_flags),
            };

            position_commands.start_requested = false;
            if (!app_supervisor_handle_event(
                    &drive_supervisor,
                    APP_EVENT_MOTION_RUN_REQUESTED,
                    energize_context) ||
                !rotor_control_runtime_request_position_relative(
                    &rotor_control_runtime,
                    position_commands.
                        requested_displacement_revolutions_q16_16,
                    position_commands.requested_maximum_velocity_q16_16,
                    position_commands.requested_maximum_acceleration_q16_16,
                    position_commands.requested_current_limit_counts,
                    position_commands.requested_duration_millis))
            {
                bridge_ready = false;
                (void)app_supervisor_handle_event(
                    &drive_supervisor,
                    APP_EVENT_FAULT_DETECTED,
                    (app_transition_context_t){0});
                board_bridge_force_low_zero();
            }
            diagnostics_due = true;
        }

        if (commissioning_context.remote_authority_active &&
            (((raw_input_levels & USER_INPUT_BUTTON_RIGHT) == 0u) ||
             (safety_housekeeping_due &&
              ((int32_t)(now - commissioning_context.
                                   remote_run_deadline_millis) >= 0))))
        {
            commissioning_context.remote_stop_requested = true;
        }

        if (commissioning_context.remote_stop_requested)
        {
            const bool was_active =
                commissioning_context.remote_authority_active;

            commissioning_context.remote_start_requested = false;
            commissioning_context.remote_authority_active = false;
            commissioning_context.remote_stop_requested = false;
            if (was_active)
            {
                if (!current_loop_backend_stop())
                {
                    board_bridge_force_low_zero();
                    platform_panic(PANIC_DRIVE_CONTROL);
                }
                current_loop_backend_get_snapshot(&current_loop_snapshot);
                if (current_loop_snapshot.fault_flags != 0u)
                {
                    bridge_ready = false;
                    (void)app_supervisor_handle_event(
                        &drive_supervisor,
                        APP_EVENT_FAULT_DETECTED,
                        (app_transition_context_t){0});
                    board_bridge_force_low_zero();
                }
                else if (!app_supervisor_handle_event(
                             &drive_supervisor,
                             APP_EVENT_AUTHORITY_RELEASED,
                             (app_transition_context_t){0}))
                {
                    board_bridge_force_low_zero();
                    platform_panic(PANIC_INTERNAL_INVARIANT);
                }
                diagnostics_due = true;
            }
        }
        else if (commissioning_context.remote_start_requested)
        {
            const app_transition_context_t energize_context = {
                .safe_to_energize =
                    bridge_ready && adc_snapshot_valid &&
                    encoder_control_ready(
                        &encoder_diagnostics,
                        &angle_tracker,
                        encoder_feedback_live,
                        estimator_fault_flags),
            };

            commissioning_context.remote_start_requested = false;
            if (bridge_ready && adc_snapshot_valid &&
                app_supervisor_handle_event(
                    &drive_supervisor,
                    APP_EVENT_DIAGNOSTIC_OPERATION_REQUESTED,
                    energize_context))
            {
                if (!current_loop_backend_set_rotating_reference(
                        (int16_t)commissioning_context.
                            test_amplitude_counts,
                        current_test_phase_increment(
                            commissioning_context.
                                test_frequency_millihz),
                        current_test_initial_phase(
                            commissioning_context.remote_start_leg),
                        current_test_ramp_step_count(
                            commissioning_context.
                                remote_start_ramp_duration_millis),
                        (current_loop_backend_controller_t)
                            commissioning_context.test_controller_mode) ||
                    !current_loop_backend_start())
                {
                    commissioning_context.remote_authority_active = false;
                    (void)app_supervisor_handle_event(
                        &drive_supervisor,
                        APP_EVENT_FAULT_DETECTED,
                        (app_transition_context_t){0});
                    board_bridge_force_low_zero();
                    platform_panic(PANIC_DRIVE_CONTROL);
                }
                commissioning_context.remote_authority_active = true;
                commissioning_context.remote_run_deadline_millis =
                    now + commissioning_context.
                              remote_start_ramp_duration_millis +
                          commissioning_context.
                              remote_start_duration_millis;
            }
            diagnostics_due = true;
        }

        if (safety_housekeeping_due)
        {
            if (commissioning_context.remote_authority_active &&
                !app_supervisor_bridge_authorized(&drive_supervisor))
            {
                commissioning_context.remote_authority_active = false;
                (void)app_supervisor_handle_event(
                    &drive_supervisor,
                    APP_EVENT_FAULT_DETECTED,
                    (app_transition_context_t){0});
                board_bridge_force_low_zero();
                platform_panic(PANIC_INTERNAL_INVARIANT);
            }
            if (((rotor_active_control_flags &
                  ROTOR_CONTROL_ACTIVE_ALIGNMENT) != 0u) &&
                ((drive_supervisor.state != APP_STATE_ALIGN) ||
                 (drive_supervisor.authority != APP_AUTHORITY_MOTION) ||
                 !app_supervisor_bridge_authorized(&drive_supervisor)))
            {
                rotor_control_runtime_force_fault(
                    &rotor_control_runtime, timebase_micros());
                (void)app_supervisor_handle_event(
                    &drive_supervisor,
                    APP_EVENT_FAULT_DETECTED,
                    (app_transition_context_t){0});
                board_bridge_force_low_zero();
                platform_panic(PANIC_INTERNAL_INVARIANT);
            }
            if (((rotor_active_control_flags &
                  ROTOR_CONTROL_ACTIVE_ALIGNED_TORQUE) != 0u) &&
                ((drive_supervisor.state != APP_STATE_RUN) ||
                 (drive_supervisor.authority != APP_AUTHORITY_MOTION) ||
                 !app_supervisor_bridge_authorized(&drive_supervisor)))
            {
                rotor_control_runtime_force_fault(
                    &rotor_control_runtime, timebase_micros());
                (void)app_supervisor_handle_event(
                    &drive_supervisor,
                    APP_EVENT_FAULT_DETECTED,
                    (app_transition_context_t){0});
                board_bridge_force_low_zero();
                platform_panic(PANIC_INTERNAL_INVARIANT);
            }
            if (((rotor_active_control_flags &
                  ROTOR_CONTROL_ACTIVE_VELOCITY) != 0u) &&
                ((drive_supervisor.state != APP_STATE_RUN) ||
                 (drive_supervisor.authority != APP_AUTHORITY_MOTION) ||
                 !app_supervisor_bridge_authorized(&drive_supervisor)))
            {
                rotor_control_runtime_force_fault(
                    &rotor_control_runtime, timebase_micros());
                (void)app_supervisor_handle_event(
                    &drive_supervisor,
                    APP_EVENT_FAULT_DETECTED,
                    (app_transition_context_t){0});
                board_bridge_force_low_zero();
                platform_panic(PANIC_INTERNAL_INVARIANT);
            }
        }

        if (adc_ready &&
            ((int32_t)(now - next_adc_sample) >= 0))
        {
            const adc1_status_t vbus_status =
                adc1_read_synchronized_vbus(&vbus_snapshot);

            if (vbus_status == ADC1_STATUS_OK)
            {
                vbus_snapshot_valid = true;
            }
            else if ((vbus_status != ADC1_STATUS_NO_SAMPLE) &&
                     (vbus_status != ADC1_STATUS_BUSY))
            {
                vbus_snapshot_valid = false;
            }
            adc_status = adc1_read_synchronized_current(&adc_snapshot);

            if (adc_status == ADC1_STATUS_OK)
            {
                adc_snapshot_valid = true;
                if (!adc_calibration_ready)
                {
                    if (!adc_zero_calibrator_observe(
                            &adc_zero_calibrator,
                            adc_snapshot.current_b_raw,
                            adc_snapshot.current_a_raw))
                    {
                        platform_panic(PANIC_INTERNAL_INVARIANT);
                    }
                    adc_calibration_ready = adc_zero_calibrator_get(
                        &adc_zero_calibrator,
                        &adc_calibration);
                }
                if (adc_calibration_ready && !current_loop_initialized)
                {
                    current_loop_config.current_a_zero_raw =
                        (uint16_t)(adc_calibration.current_a_zero_raw + 0.5f);
                    current_loop_config.current_b_zero_raw =
                        (uint16_t)(adc_calibration.current_b_zero_raw + 0.5f);
                    if (!current_loop_backend_init(
                            &current_loop_config,
                            &phase_predictor_config))
                    {
                        board_bridge_force_low_zero();
                        platform_panic(PANIC_DRIVE_CONTROL);
                    }
                    current_loop_initialized = true;
                    bridge_ready = true;
                }
                if (adc_calibration_ready &&
                    !adc_current_pair_convert_milliamperes(
                        adc_snapshot.current_b_raw,
                        adc_snapshot.current_a_raw,
                        &adc_calibration,
                        &current_b_milliamperes,
                        &current_a_milliamperes))
                {
                    platform_panic(PANIC_INTERNAL_INVARIANT);
                }
            }
            else if ((adc_status != ADC1_STATUS_NO_SAMPLE) &&
                     (adc_status != ADC1_STATUS_BUSY))
            {
                adc_ready = false;
                adc_snapshot_valid = false;
                vbus_snapshot_valid = false;
                rotor_control_runtime_force_fault(
                    &rotor_control_runtime, timebase_micros());
                commissioning_context.remote_authority_active = false;
                bridge_ready = false;
                commissioning_context.remote_start_requested = false;
                commissioning_context.remote_stop_requested = false;
                commissioning_context.alignment_start_requested = false;
                commissioning_context.alignment_stop_requested = false;
                commissioning_context.torque_start_requested = false;
                commissioning_context.torque_stop_requested = false;
                velocity_commands.start_requested = false;
                velocity_commands.stop_requested = false;
                position_commands.start_requested = false;
                position_commands.stop_requested = false;
                (void)app_supervisor_handle_event(
                    &drive_supervisor,
                    APP_EVENT_FAULT_DETECTED,
                    (app_transition_context_t){0});
                board_bridge_force_low_zero();
                diagnostics_due = true;
            }
            if (current_loop_initialized)
            {
                current_loop_backend_get_snapshot(&current_loop_snapshot);
                update_current_loop_diagnostics(
                    &current_loop_snapshot,
                    &current_loop_diagnostics);
                diagnostics_publish_current_loop(
                    &current_loop_diagnostics);
                if (current_loop_snapshot.fault_flags != 0u)
                {
                    rotor_control_runtime_force_fault(
                        &rotor_control_runtime, timebase_micros());
                    commissioning_context.remote_authority_active = false;
                    commissioning_context.remote_start_requested = false;
                    commissioning_context.remote_stop_requested = false;
                    commissioning_context.alignment_start_requested = false;
                    commissioning_context.alignment_stop_requested = false;
                    commissioning_context.torque_start_requested = false;
                    commissioning_context.torque_stop_requested = false;
                    velocity_commands.start_requested = false;
                    velocity_commands.stop_requested = false;
                    position_commands.start_requested = false;
                    position_commands.stop_requested = false;
                    bridge_ready = false;
                    (void)app_supervisor_handle_event(
                        &drive_supervisor,
                        APP_EVENT_FAULT_DETECTED,
                        (app_transition_context_t){0});
                    board_bridge_force_low_zero();
                    diagnostics_due = true;
                    if (current_loop_fault_code == 0u)
                    {
                        current_loop_fault_code =
                            current_loop_fault_display_code(
                                current_loop_snapshot.fault_flags);
                        next_display_refresh = now;
                    }
                }
                else if ((commissioning_context.remote_authority_active ||
                          alignment_controller_is_active(
                              &alignment_controller) ||
                          aligned_torque_controller_is_active(
                              &aligned_torque_controller)) &&
                         !current_loop_snapshot.active)
                {
                    rotor_control_runtime_force_fault(
                        &rotor_control_runtime, timebase_micros());
                    commissioning_context.remote_authority_active = false;
                    commissioning_context.torque_start_requested = false;
                    commissioning_context.torque_stop_requested = false;
                    velocity_commands.start_requested = false;
                    velocity_commands.stop_requested = false;
                    position_commands.start_requested = false;
                    position_commands.stop_requested = false;
                    bridge_ready = false;
                    (void)app_supervisor_handle_event(
                        &drive_supervisor,
                        APP_EVENT_FAULT_DETECTED,
                        (app_transition_context_t){0});
                    board_bridge_force_low_zero();
                    diagnostics_due = true;
                }
            }
            next_adc_sample = now + ADC_SNAPSHOT_PERIOD_MS;
        }

        if (safety_housekeeping_due)
        {
            const bool drive_prerequisites_ready =
                bridge_ready && current_loop_initialized &&
                adc_snapshot_valid &&
                encoder_control_ready(
                    &encoder_diagnostics,
                    &angle_tracker,
                    encoder_feedback_live,
                    estimator_fault_flags);

            if ((drive_supervisor.state == APP_STATE_DIAGNOSTIC) &&
                drive_prerequisites_ready)
            {
                if (!app_supervisor_handle_event(
                        &drive_supervisor,
                        APP_EVENT_READINESS_CONFIRMED,
                        (app_transition_context_t){
                            .safe_to_energize = true,
                        }))
                {
                    platform_panic(PANIC_INTERNAL_INVARIANT);
                }
                diagnostics_due = true;
            }
            else if (((drive_supervisor.state == APP_STATE_READY) ||
                      (drive_supervisor.state == APP_STATE_ALIGN) ||
                      (drive_supervisor.state == APP_STATE_RUN)) &&
                     !drive_prerequisites_ready)
            {
                const bool motion_was_active =
                    (rotor_active_control_flags &
                     (ROTOR_CONTROL_ACTIVE_ALIGNED_TORQUE |
                      ROTOR_CONTROL_ACTIVE_VELOCITY |
                      ROTOR_CONTROL_ACTIVE_POSITION)) != 0u;

                if (app_supervisor_bridge_authorized(&drive_supervisor))
                {
                    rotor_control_runtime_force_fault(
                        &rotor_control_runtime, timebase_micros());
                    commissioning_context.remote_authority_active = false;
                    commissioning_context.remote_start_requested = false;
                    commissioning_context.remote_stop_requested = false;
                    commissioning_context.alignment_start_requested = false;
                    commissioning_context.alignment_stop_requested = false;
                    commissioning_context.torque_start_requested = false;
                    commissioning_context.torque_stop_requested = false;
                    velocity_commands.start_requested = false;
                    velocity_commands.stop_requested = false;
                    position_commands.start_requested = false;
                    position_commands.stop_requested = false;
                }
                if (!app_supervisor_handle_event(
                        &drive_supervisor,
                        motion_was_active ?
                            APP_EVENT_FAULT_DETECTED :
                            APP_EVENT_READINESS_LOST,
                        (app_transition_context_t){0}))
                {
                    board_bridge_force_low_zero();
                    platform_panic(PANIC_INTERNAL_INVARIANT);
                }
                diagnostics_due = true;
            }
        }

        if ((int32_t)(now - next_heartbeat) >= 0)
        {
            board_status_led_toggle();
            ++heartbeat_count;
            next_heartbeat += 250u;
            diagnostics_due = true;
        }

        if (safety_housekeeping_due)
        {
            watchdog_status = watchdog_supervisor_poll(
                &watchdog,
                now,
                app_supervisor_foreground_service_allowed(
                    &drive_supervisor) &&
                    boot_self_test_ready(&self_test));
            if (watchdog_status != WATCHDOG_STATUS_READY)
            {
                diagnostics_publish((uint32_t)drive_supervisor.state,
                                    now,
                                    heartbeat_count,
                                    (uint32_t)watchdog_status,
                                    &self_test);
                platform_panic(PANIC_WATCHDOG_LIVENESS);
            }
        }

        if (diagnostics_due)
        {
            diagnostics_publish_rs485(&rs485_diagnostics);
            diagnostics_publish_protocol(&protocol_diagnostics);
            diagnostics_publish((uint32_t)drive_supervisor.state,
                                now,
                                heartbeat_count,
                                (uint32_t)watchdog_status,
                                &self_test);
        }

        if (display_ready &&
            !app_supervisor_bridge_authorized(&drive_supervisor) &&
            ((int32_t)(now - next_display_refresh) >= 0))
        {
            bool rendered;

            if (current_loop_fault_code != 0u)
            {
                rendered = adc_display_render(
                    s_display_frame,
                    ADC_DISPLAY_FRAME_BYTES,
                    ADC_DISPLAY_FAULT,
                    current_loop_fault_code,
                    true);
            }
            else if (!adc_ready)
            {
                rendered = adc_display_render(
                    s_display_frame,
                    ADC_DISPLAY_FRAME_BYTES,
                    ADC_DISPLAY_CURRENT_A,
                    (uint16_t)adc_status,
                    true);
            }
            else
            {
                rendered = adc_display_render_currents_milliamperes(
                    s_display_frame,
                    ADC_DISPLAY_FRAME_BYTES,
                    current_a_milliamperes,
                    current_b_milliamperes,
                    adc_snapshot_valid && adc_calibration_ready);
            }

            if (!rendered ||
                (ssd1306_write_pages(
                     &display_bus,
                     &SSD1306_PANEL_SERVO57D,
                     ADC_DISPLAY_START_PAGE,
                     ADC_DISPLAY_PAGE_COUNT,
                     s_display_frame,
                     ADC_DISPLAY_FRAME_BYTES) != I2C_STATUS_OK))
            {
                display_ready = false;
            }
            next_display_refresh = now + DISPLAY_REFRESH_PERIOD_MS;
        }

        if (runtime_profile_foreground_active)
        {
            runtime_profile_foreground_complete(
                runtime_profile_foreground_start_cycle_count,
                cycle_counter_read());
        }

        __WFI();
    }
}
