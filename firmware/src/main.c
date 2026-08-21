#include <stdbool.h>
#include <limits.h>
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
#include "mks57d/current_loop_backend.h"
#include "mks57d/diagnostics.h"
#include "mks57d/i2c1.h"
#include "mks57d/interrupt_priority.h"
#include "mks57d/mt6816.h"
#include "mks57d/motor_alignment.h"
#include "mks57d/native_protocol.h"
#include "mks57d/panic.h"
#include "mks57d/platform.h"
#include "mks57d/rs485.h"
#include "mks57d/rotating_current_test.h"
#include "mks57d/rotor_control_runtime.h"
#include "mks57d/spi1.h"
#include "mks57d/ssd1306.h"
#include "mks57d/timebase.h"
#include "mks57d/tim2_current_trigger.h"
#include "mks57d/tim3_bridge_pwm.h"
#include "mks57d/user_inputs.h"
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

enum
{
    DISPLAY_WIDTH = 72u,
    DISPLAY_HEIGHT = 40u,
    DISPLAY_FRAME_BYTES = DISPLAY_WIDTH * (DISPLAY_HEIGHT / 8u)
};

static uint8_t s_display_frame[DISPLAY_FRAME_BYTES];
static rotor_control_runtime_t rotor_control_runtime;
static rotor_control_snapshot_t rotor_control_snapshot;

enum
{
    COMMISSIONING_STATUS_SCHEMA_VERSION = 2u,
    ENCODER_STATUS_SCHEMA_VERSION = 2u,
    CURRENT_TRACE_SCHEMA_VERSION = 1u,
    ALIGNMENT_STATUS_SCHEMA_VERSION = 1u,
    ALIGNED_TORQUE_STATUS_SCHEMA_VERSION = 1u,
    CURRENT_TEST_MINIMUM_FREQUENCY_MILLIHZ = 1u,
    CURRENT_TEST_MAXIMUM_FREQUENCY_MILLIHZ = 250000u,
    CURRENT_TEST_MINIMUM_REMOTE_DURATION_MS = 3u,
    CURRENT_TEST_MAXIMUM_REMOTE_DURATION_MS = INT32_MAX,
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
typedef struct
{
    app_supervisor_t* supervisor;
    bool* adc_ready;
    bool* adc_snapshot_valid;
    bool* adc_calibration_ready;
    bool* current_loop_initialized;
    bool* bridge_ready;
    adc1_status_t* adc_status;
    adc1_current_snapshot_t* adc_snapshot;
    adc_calibration_t* adc_calibration;
    diagnostics_encoder_t* encoder_diagnostics;
    angle_tracker_t* angle_tracker;
    uint32_t* estimator_fault_flags;
    uint32_t* estimator_sample_interval_us;
    uint32_t* estimator_maximum_sample_interval_us;
    motor_alignment_t* motor_alignment;
    alignment_controller_t* alignment_controller;
    aligned_torque_controller_t* aligned_torque_controller;
    rotor_control_runtime_t* rotor_control_runtime;
    configuration_store_t* configuration_store;
    uint32_t* raw_input_levels;
    uint32_t* input_levels;
    const phase_current_loop_config_t* current_loop_config;
    uint16_t maximum_test_amplitude_counts;
    uint16_t test_amplitude_counts;
    uint32_t test_frequency_millihz;
    bool remote_start_requested;
    bool remote_stop_requested;
    bool remote_authority_active;
    uint8_t remote_start_leg;
    uint32_t remote_start_duration_millis;
    uint32_t remote_run_deadline_millis;
    uint16_t alignment_current_counts;
    bool alignment_start_requested;
    bool alignment_stop_requested;
    int16_t torque_q_current_counts;
    uint32_t torque_duration_millis;
    bool torque_start_requested;
    bool torque_stop_requested;
} product_command_context_t;

static bool encoder_control_ready(
    const diagnostics_encoder_t* encoder_diagnostics,
    const angle_tracker_t* angle_tracker,
    uint32_t estimator_fault_flags)
{
    return (encoder_diagnostics != NULL) &&
           (angle_tracker != NULL) &&
           (encoder_diagnostics->status == (uint32_t)MT6816_STATUS_OK) &&
           (encoder_diagnostics->transport_status ==
            (uint32_t)SPI_STATUS_OK) &&
           (encoder_diagnostics->flags == 0u) &&
           (encoder_diagnostics->sample_count != 0u) &&
           angle_tracker->initialized &&
           (estimator_fault_flags == 0u);
}

static int32_t float_to_q16_16(float value)
{
    const float maximum = 32767.9999847412109375f;

    if (value >= maximum)
    {
        return INT32_MAX;
    }
    if (value <= -32768.0f)
    {
        return INT32_MIN;
    }
    return (int32_t)(value * 65536.0f);
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
    const uint64_t numerator =
        ((uint64_t)frequency_millihz << 32u) + 500000u;

    return (uint32_t)(numerator / 1000000u);
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
         CURRENT_TEST_MAXIMUM_FREQUENCY_MILLIHZ))
    {
        return COMMAND_STATUS_INVALID_PAYLOAD;
    }
    if (app_supervisor_bridge_authorized(commissioning->supervisor) ||
        commissioning->remote_start_requested ||
        commissioning->alignment_start_requested ||
        commissioning->torque_start_requested ||
        aligned_torque_controller_is_active(
            commissioning->aligned_torque_controller) ||
        alignment_controller_is_active(
            commissioning->alignment_controller))
    {
        return COMMAND_STATUS_UNAVAILABLE;
    }

    commissioning->test_amplitude_counts = requested->amplitude_counts;
    commissioning->test_frequency_millihz = requested->frequency_millihz;
    *applied = *requested;
    return COMMAND_STATUS_OK;
}

static command_status_t commissioning_start(
    void* context,
    uint8_t selected_leg,
    uint32_t duration_millis)
{
    product_command_context_t* commissioning = context;
    current_loop_backend_snapshot_t loop = {0};

    if (commissioning == NULL)
    {
        return COMMAND_STATUS_INTERNAL_ERROR;
    }
    if ((selected_leg >= CURRENT_TEST_INITIAL_LEG_COUNT) ||
        (duration_millis < CURRENT_TEST_MINIMUM_REMOTE_DURATION_MS) ||
        (duration_millis > CURRENT_TEST_MAXIMUM_REMOTE_DURATION_MS))
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
        commissioning->alignment_start_requested ||
        commissioning->torque_start_requested ||
        aligned_torque_controller_is_active(
            commissioning->aligned_torque_controller) ||
        alignment_controller_is_active(
            commissioning->alignment_controller) ||
        (loop.fault_flags != 0u) ||
        ((*commissioning->raw_input_levels & USER_INPUT_KEY_MENU) == 0u))
    {
        return COMMAND_STATUS_UNAVAILABLE;
    }

    commissioning->remote_start_leg = selected_leg;
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
        commissioning->alignment_start_requested ||
        commissioning->torque_start_requested ||
        aligned_torque_controller_is_active(
            commissioning->aligned_torque_controller) ||
        alignment_controller_is_active(
            commissioning->alignment_controller) ||
        (loop.fault_flags != 0u) ||
        ((*commissioning->raw_input_levels & USER_INPUT_KEY_MENU) == 0u))
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
            *commissioning->estimator_fault_flags) ||
        app_supervisor_bridge_authorized(commissioning->supervisor) ||
        commissioning->remote_start_requested ||
        commissioning->alignment_start_requested ||
        commissioning->torque_start_requested ||
        alignment_controller_is_active(
            commissioning->alignment_controller) ||
        aligned_torque_controller_is_active(
            commissioning->aligned_torque_controller) ||
        loop.active || (loop.fault_flags != 0u) ||
        ((*commissioning->raw_input_levels & USER_INPUT_KEY_MENU) == 0u))
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
        controller_status.current_a_reference_counts;
    status->current_b_reference_counts =
        controller_status.current_b_reference_counts;
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
    return COMMAND_STATUS_OK;
}

static bool build_product_configuration(
    const motor_alignment_t* motor_alignment,
    product_configuration_t* configuration)
{
    if ((motor_alignment == NULL) || (configuration == NULL) ||
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
           !loop.active && (loop.fault_flags == 0u) &&
           !alignment_controller_is_active(
                commissioning->alignment_controller) &&
           !aligned_torque_controller_is_active(
                commissioning->aligned_torque_controller) &&
           !commissioning->remote_start_requested &&
           !commissioning->remote_stop_requested &&
           !commissioning->alignment_start_requested &&
           !commissioning->alignment_stop_requested &&
           !commissioning->torque_start_requested &&
           !commissioning->torque_stop_requested;
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
            commissioning->motor_alignment, &active))
    {
        return COMMAND_STATUS_INTERNAL_ERROR;
    }

    store = commissioning->configuration_store;
    memset(status, 0, sizeof(*status));
    status->schema_version = 1u;
    status->active_slot = CONFIGURATION_STORE_INVALID_SLOT;
    status->record_schema_version =
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
    }
    return COMMAND_STATUS_OK;
}

static command_status_t configuration_save_active(void* context)
{
    product_command_context_t* commissioning = context;
    product_configuration_t configuration;

    if ((commissioning == NULL) ||
        !build_product_configuration(
            commissioning->motor_alignment, &configuration))
    {
        return COMMAND_STATUS_INTERNAL_ERROR;
    }
    if (!configuration.alignment.valid ||
        !configuration_write_allowed(commissioning))
    {
        return COMMAND_STATUS_UNAVAILABLE;
    }

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
            commissioning->motor_alignment, &configuration))
    {
        return COMMAND_STATUS_INTERNAL_ERROR;
    }
    if (!configuration_write_allowed(commissioning))
    {
        return COMMAND_STATUS_UNAVAILABLE;
    }

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
        status->estimator_flags |= COMMAND_ENCODER_ESTIMATOR_READY;
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
    return COMMAND_STATUS_OK;
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
        CURRENT_TEST_REFERENCE_PERIOD_MS = 1u,
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
        ALIGNED_TORQUE_MAXIMUM_VELOCITY_Q16_16 = 5u << 16,
        ALIGNED_TORQUE_MAXIMUM_ACCELERATION_Q16_16 = 1000u << 16,
        ALIGNED_TORQUE_MAXIMUM_FEEDBACK_INTERVAL_US = 2000u,
        /*
         * Three milliseconds allows one reference update before the deadline
         * even when the first accepted 1 kHz feedback sample arrives at the
         * full two-millisecond feedback-age limit.
         */
        ALIGNED_TORQUE_MINIMUM_DURATION_MS = 3u,
        /*
         * The requested duration is the operation's explicit finite deadline,
         * not a proxy for current or thermal capability. Keep it within one
         * signed half-range so the wrap-safe deadline comparisons are
         * unambiguous for every accepted start time.
         */
        ALIGNED_TORQUE_MAXIMUM_DURATION_MS = INT32_MAX,
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
    _Static_assert(ALIGNED_TORQUE_MAXIMUM_CURRENT_COUNTS <=
                       CURRENT_LOOP_REFERENCE_LIMIT_COUNTS,
                   "torque policy exceeds the current backend contract");
    app_supervisor_t drive_supervisor;
    angle_tracker_t angle_tracker;
    const angle_tracker_config_t angle_tracker_config = {
        .counts_per_revolution = ENCODER_COUNTS_PER_REVOLUTION,
        .maximum_sample_interval_us =
            ENCODER_MAXIMUM_SAMPLE_INTERVAL_US,
        .maximum_velocity_revolutions_per_second = 20.0f,
        .velocity_filter_alpha = 0.125f,
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
    adc_zero_calibrator_t adc_zero_calibrator;
    adc_calibration_t adc_calibration = {0};
    phase_current_loop_config_t current_loop_config = {
        .reference_limit_counts = CURRENT_LOOP_REFERENCE_LIMIT_COUNTS,
        .hard_current_limit_counts = CURRENT_LOOP_HARD_LIMIT_COUNTS,
        .proportional_gain_q16_per_count =
            2 * (int32_t)PHASE_CURRENT_LOOP_Q16_ONE,
        .integral_gain_q16_per_count_per_step =
            (int32_t)PHASE_CURRENT_LOOP_Q16_ONE / 64,
        .phase_voltage_limit_permille =
            CURRENT_LOOP_PHASE_VOLTAGE_LIMIT_PERMILLE,
        .duty_margin_permille = CURRENT_LOOP_DUTY_MARGIN_PERMILLE,
        .current_a_polarity = 1,
        .current_b_polarity = 1,
    };
    current_loop_backend_snapshot_t current_loop_snapshot = {0};
    rotating_current_test_t current_test_generator = {0};
    i2c_bus_t display_bus = {0};
    bool display_ready = false;
    bool adc_ready = false;
    bool adc_snapshot_valid = false;
    bool adc_calibration_ready = false;
    bool current_loop_initialized = false;
    uint16_t current_loop_fault_code = 0u;
    int32_t current_a_milliamperes = 0;
    int32_t current_b_milliamperes = 0;
    adc1_status_t adc_status = ADC1_STATUS_NOT_READY;
    bool bridge_ready = false;
    bool encoder_spi_ready = false;
    uint32_t estimator_fault_flags = 0u;
    uint32_t estimator_sample_interval_us = 0u;
    uint32_t estimator_maximum_sample_interval_us = 0u;
    bool inputs_ready = false;
    bool rs485_ready = false;
    uint32_t heartbeat_count = 0u;
    uint32_t next_heartbeat;
    uint32_t last_encoder_diagnostics_sample_count = 0u;
    uint32_t last_encoder_diagnostics_error_count = 0u;
    uint32_t last_rotor_snapshot_millis = UINT32_MAX;
    uint32_t next_adc_sample;
    uint32_t next_input_sample;
    uint32_t next_current_reference;
    uint32_t next_display_refresh;
    uint32_t input_levels = USER_INPUT_MASK;
    uint32_t raw_input_levels = USER_INPUT_MASK;
    user_inputs_debouncer_t input_debouncer = {0};
    uint32_t uptime_millis = 0u;
    watchdog_supervisor_t watchdog;
    watchdog_status_t watchdog_status = WATCHDOG_STATUS_NOT_STARTED;
    product_command_context_t commissioning_context = {
        .supervisor = &drive_supervisor,
        .adc_ready = &adc_ready,
        .adc_snapshot_valid = &adc_snapshot_valid,
        .adc_calibration_ready = &adc_calibration_ready,
        .current_loop_initialized = &current_loop_initialized,
        .bridge_ready = &bridge_ready,
        .adc_status = &adc_status,
        .adc_snapshot = &adc_snapshot,
        .adc_calibration = &adc_calibration,
        .encoder_diagnostics = &encoder_diagnostics,
        .angle_tracker = &angle_tracker,
        .estimator_fault_flags = &estimator_fault_flags,
        .estimator_sample_interval_us =
            &estimator_sample_interval_us,
        .estimator_maximum_sample_interval_us =
            &estimator_maximum_sample_interval_us,
        .motor_alignment = &motor_alignment,
        .alignment_controller = &alignment_controller,
        .aligned_torque_controller = &aligned_torque_controller,
        .rotor_control_runtime = &rotor_control_runtime,
        .configuration_store = &configuration_store,
        .raw_input_levels = &raw_input_levels,
        .input_levels = &input_levels,
        .current_loop_config = &current_loop_config,
        .maximum_test_amplitude_counts =
            CURRENT_LOOP_REFERENCE_LIMIT_COUNTS,
        .test_amplitude_counts = CURRENT_TEST_AMPLITUDE_COUNTS,
        .test_frequency_millihz = CURRENT_TEST_FREQUENCY_MILLIHZ,
    };
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
        },
        .alignment = {
            .context = &commissioning_context,
            .start = alignment_start,
            .get_status = alignment_get_status,
        },
        .drive = {
            .context = &commissioning_context,
            .stop = commissioning_stop,
        },
        .configuration = {
            .context = &commissioning_context,
            .get_status = configuration_get_status,
            .save = configuration_save_active,
            .clear_calibration = configuration_clear_calibration,
        },
        .aligned_torque = {
            .context = &commissioning_context,
            .start = aligned_torque_start,
            .get_status = aligned_torque_get_status,
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
        !motor_alignment_init(
            &motor_alignment, &motor_alignment_config) ||
        !alignment_controller_init(
            &alignment_controller, &alignment_controller_config) ||
        !aligned_torque_controller_init(
            &aligned_torque_controller, &aligned_torque_config))
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
            &configuration_store, &stored_configuration) &&
        stored_configuration.alignment.valid &&
        (stored_configuration.encoder_counts_per_revolution ==
         motor_alignment.config.encoder_counts_per_revolution) &&
        (stored_configuration.electrical_cycles_per_revolution ==
         motor_alignment.config.electrical_cycles_per_revolution))
    {
        (void)motor_alignment_restore(
            &motor_alignment, &stored_configuration.alignment);
    }
    if (!rotor_control_runtime_init(
            &rotor_control_runtime,
            &angle_tracker,
            &motor_alignment,
            &alignment_controller,
            &aligned_torque_controller) ||
        !rotor_control_runtime_get_snapshot(
            &rotor_control_runtime, &rotor_control_snapshot))
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
        encoder_diagnostics =
            rotor_control_snapshot.encoder_diagnostics;
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
    next_adc_sample = timebase_millis();
    next_input_sample = timebase_millis();
    next_current_reference = timebase_millis();
    next_display_refresh =
        timebase_millis() + ENCODER_POWER_UP_DELAY_MS;

    for (;;)
    {
        bool diagnostics_due = false;
        const uint32_t now = timebase_millis();
        const uint32_t rotor_events =
            rotor_control_runtime_take_events(&rotor_control_runtime);
        if ((now != last_rotor_snapshot_millis) ||
            (rotor_events != ROTOR_CONTROL_EVENT_NONE))
        {
            if (!rotor_control_runtime_get_snapshot(
                    &rotor_control_runtime, &rotor_control_snapshot))
            {
                board_bridge_force_low_zero();
                platform_panic(PANIC_INTERNAL_INVARIANT);
            }
            encoder_diagnostics =
                rotor_control_snapshot.encoder_diagnostics;
            angle_tracker = rotor_control_snapshot.angle_tracker;
            motor_alignment = rotor_control_snapshot.motor_alignment;
            alignment_controller =
                rotor_control_snapshot.alignment_controller;
            aligned_torque_controller =
                rotor_control_snapshot.torque_controller;
            estimator_fault_flags =
                rotor_control_snapshot.estimator_fault_flags;
            estimator_sample_interval_us =
                rotor_control_snapshot.estimator_sample_interval_us;
            estimator_maximum_sample_interval_us =
                rotor_control_snapshot.
                    estimator_maximum_sample_interval_us;
            last_rotor_snapshot_millis = now;
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
            (void)configuration_save_active(&commissioning_context);
            diagnostics_due = true;
        }
        else if ((rotor_events &
                  ROTOR_CONTROL_EVENT_AUTHORITY_RELEASED) != 0u)
        {
            commissioning_context.alignment_start_requested = false;
            commissioning_context.alignment_stop_requested = false;
            commissioning_context.torque_start_requested = false;
            commissioning_context.torque_stop_requested = false;
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
            update_rs485_diagnostics(&rs485_diagnostics);
            if (rs485_diagnostics.status != (uint32_t)RS485_STATUS_OK)
            {
                rs485_ready = false;
                commissioning_context.remote_stop_requested = true;
                commissioning_context.alignment_stop_requested = true;
                commissioning_context.torque_stop_requested = true;
                diagnostics_due = true;
            }
        }

        if (alignment_controller_is_active(&alignment_controller) &&
            ((raw_input_levels & USER_INPUT_KEY_MENU) == 0u))
        {
            commissioning_context.alignment_stop_requested = true;
        }
        if ((commissioning_context.torque_start_requested ||
             aligned_torque_controller_is_active(
                 &aligned_torque_controller)) &&
            ((raw_input_levels & USER_INPUT_KEY_MENU) == 0u))
        {
            commissioning_context.torque_stop_requested = true;
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

        if (commissioning_context.torque_stop_requested)
        {
            commissioning_context.torque_start_requested = false;
            commissioning_context.torque_stop_requested = false;
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

        if (commissioning_context.remote_authority_active &&
            (((raw_input_levels & USER_INPUT_KEY_MENU) == 0u) ||
             ((int32_t)(now - commissioning_context.
                                  remote_run_deadline_millis) >= 0)))
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
                        estimator_fault_flags),
            };

            commissioning_context.remote_start_requested = false;
            if (bridge_ready && adc_snapshot_valid &&
                app_supervisor_handle_event(
                    &drive_supervisor,
                    APP_EVENT_DIAGNOSTIC_OPERATION_REQUESTED,
                    energize_context))
            {
                int16_t current_a_reference_counts;
                int16_t current_b_reference_counts;

                if (!rotating_current_test_init(
                        &current_test_generator,
                        (int16_t)commissioning_context.
                            test_amplitude_counts,
                        current_test_phase_increment(
                            commissioning_context.
                                test_frequency_millihz),
                        current_test_initial_phase(
                            commissioning_context.remote_start_leg)) ||
                    !rotating_current_test_step(
                        &current_test_generator,
                        &current_a_reference_counts,
                        &current_b_reference_counts) ||
                    !current_loop_backend_set_reference_counts(
                        current_a_reference_counts,
                        current_b_reference_counts) ||
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
                              remote_start_duration_millis;
                next_current_reference =
                    now + CURRENT_TEST_REFERENCE_PERIOD_MS;
            }
            diagnostics_due = true;
        }

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
        if (alignment_controller_is_active(&alignment_controller) &&
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
        if (aligned_torque_controller_is_active(
                &aligned_torque_controller) &&
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

        if (app_supervisor_bridge_authorized(&drive_supervisor) &&
            (drive_supervisor.authority == APP_AUTHORITY_DIAGNOSTIC) &&
            commissioning_context.remote_authority_active &&
            ((int32_t)(now - next_current_reference) >= 0))
        {
            int16_t current_a_reference_counts;
            int16_t current_b_reference_counts;

            if (!rotating_current_test_step(
                    &current_test_generator,
                    &current_a_reference_counts,
                    &current_b_reference_counts))
            {
                board_bridge_force_low_zero();
                platform_panic(PANIC_DRIVE_CONTROL);
            }
            if (!current_loop_backend_set_reference_counts(
                    current_a_reference_counts,
                    current_b_reference_counts))
            {
                current_loop_backend_get_snapshot(&current_loop_snapshot);
                commissioning_context.remote_authority_active = false;
                bridge_ready = false;
                (void)app_supervisor_handle_event(
                    &drive_supervisor,
                    APP_EVENT_FAULT_DETECTED,
                    (app_transition_context_t){0});
                if ((current_loop_snapshot.fault_flags == 0u) ||
                    !current_loop_backend_stop())
                {
                    board_bridge_force_low_zero();
                    platform_panic(PANIC_DRIVE_CONTROL);
                }
                current_loop_backend_get_snapshot(&current_loop_snapshot);
                update_current_loop_diagnostics(
                    &current_loop_snapshot,
                    &current_loop_diagnostics);
                diagnostics_publish_current_loop(
                    &current_loop_diagnostics);
                if (current_loop_fault_code == 0u)
                {
                    current_loop_fault_code =
                        current_loop_fault_display_code(
                            current_loop_snapshot.fault_flags);
                    next_display_refresh = now;
                }
                diagnostics_due = true;
            }
            next_current_reference =
                now + CURRENT_TEST_REFERENCE_PERIOD_MS;
        }

        if (adc_ready &&
            ((int32_t)(now - next_adc_sample) >= 0))
        {
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
                    if (!current_loop_backend_init(&current_loop_config))
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

        {
            const bool drive_prerequisites_ready =
                bridge_ready && current_loop_initialized &&
                adc_snapshot_valid &&
                encoder_control_ready(
                    &encoder_diagnostics,
                    &angle_tracker,
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
                const bool torque_was_active =
                    aligned_torque_controller_is_active(
                        &aligned_torque_controller);

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
                }
                if (!app_supervisor_handle_event(
                        &drive_supervisor,
                        torque_was_active ?
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

        watchdog_status = watchdog_supervisor_poll(
            &watchdog,
            now,
            app_supervisor_foreground_service_allowed(&drive_supervisor) &&
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

        __WFI();
    }
}
