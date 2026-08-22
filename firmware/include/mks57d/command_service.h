#ifndef MKS57D_COMMAND_SERVICE_H
#define MKS57D_COMMAND_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum
{
    COMMAND_SERVICE_PRODUCT_ID_MKS57D = 0x4D4B5335u,
    COMMAND_SERVICE_MAX_PING_BYTES = 16u
};

typedef enum
{
    COMMAND_OPERATION_PING = 0,
    COMMAND_OPERATION_GET_IDENTITY,
    COMMAND_OPERATION_GET_CAPABILITIES,
    COMMAND_OPERATION_GET_COMMISSIONING_STATUS,
    COMMAND_OPERATION_CONFIGURE_CURRENT_TEST,
    COMMAND_OPERATION_START_CURRENT_TEST,
    COMMAND_OPERATION_STOP_CURRENT_TEST,
    COMMAND_OPERATION_GET_BOOT_STATUS,
    COMMAND_OPERATION_GET_ENCODER_STATUS,
    COMMAND_OPERATION_GET_CURRENT_TRACE,
    COMMAND_OPERATION_START_ALIGNMENT,
    COMMAND_OPERATION_GET_ALIGNMENT_STATUS,
    COMMAND_OPERATION_STOP_DRIVE,
    COMMAND_OPERATION_GET_CONFIGURATION_STATUS,
    COMMAND_OPERATION_SAVE_CONFIGURATION,
    COMMAND_OPERATION_CLEAR_CALIBRATION,
    COMMAND_OPERATION_START_ALIGNED_TORQUE,
    COMMAND_OPERATION_GET_ALIGNED_TORQUE_STATUS,
    COMMAND_OPERATION_START_VELOCITY,
    COMMAND_OPERATION_GET_VELOCITY_STATUS
} command_operation_t;

typedef enum
{
    COMMAND_STATUS_OK = 0,
    COMMAND_STATUS_UNKNOWN_COMMAND,
    COMMAND_STATUS_INVALID_PAYLOAD,
    COMMAND_STATUS_UNAVAILABLE,
    COMMAND_STATUS_INTERNAL_ERROR
} command_status_t;

typedef enum
{
    COMMAND_COMMISSIONING_FLAG_ADC_READY = 1u << 0,
    COMMAND_COMMISSIONING_FLAG_ADC_SNAPSHOT_VALID = 1u << 1,
    COMMAND_COMMISSIONING_FLAG_ADC_CALIBRATION_READY = 1u << 2,
    COMMAND_COMMISSIONING_FLAG_CURRENT_LOOP_INITIALIZED = 1u << 3,
    COMMAND_COMMISSIONING_FLAG_BRIDGE_READY = 1u << 4,
    COMMAND_COMMISSIONING_FLAG_AUTHORITY_ACTIVE = 1u << 5,
    COMMAND_COMMISSIONING_FLAG_BACKEND_ACTIVE = 1u << 6,
    COMMAND_COMMISSIONING_FLAG_REMOTE_AUTHORITY = 1u << 7,
    COMMAND_COMMISSIONING_FLAG_REMOTE_START_PENDING = 1u << 8,
    COMMAND_COMMISSIONING_FLAG_REMOTE_STOP_PENDING = 1u << 9,
    COMMAND_COMMISSIONING_FLAG_FAULT_PRESENT = 1u << 10
} command_commissioning_flag_t;

typedef struct
{
    uint16_t amplitude_counts;
    uint32_t frequency_millihz;
} command_current_test_config_t;

typedef struct
{
    uint8_t schema_version;
    uint32_t flags;
    uint8_t raw_input_levels;
    uint8_t debounced_input_levels;
    uint8_t adc_status;
    uint8_t selected_leg;
    uint32_t fault_flags;
    uint32_t sample_count;
    uint16_t current_a_raw;
    uint16_t current_b_raw;
    uint16_t current_a_zero_raw;
    uint16_t current_b_zero_raw;
    int16_t current_a_reference_counts;
    int16_t current_b_reference_counts;
    int16_t current_a_measured_counts;
    int16_t current_b_measured_counts;
    int16_t phase_a_voltage_permille;
    int16_t phase_b_voltage_permille;
    uint16_t duty_a1_permille;
    uint16_t duty_a2_permille;
    uint16_t duty_b1_permille;
    uint16_t duty_b2_permille;
    uint16_t test_amplitude_counts;
    uint16_t maximum_test_amplitude_counts;
    uint16_t hard_current_limit_counts;
    uint16_t phase_voltage_limit_permille;
    uint32_t test_frequency_millihz;
    uint32_t remote_run_remaining_millis;
    uint8_t retained_panic;
    uint8_t watchdog_reset;
} command_commissioning_status_t;

typedef struct
{
    uint8_t schema_version;
    uint32_t reset_flags;
    uint8_t retained_panic;
    uint32_t uptime_millis;
} command_boot_status_t;

typedef struct
{
    uint8_t schema_version;
    uint8_t status;
    uint8_t transport_status;
    uint16_t angle_raw;
    uint8_t flags;
    uint32_t sample_count;
    uint32_t error_count;
    uint32_t last_attempt_millis;
    uint8_t estimator_flags;
    int32_t position_revolutions_q16_16;
    int32_t velocity_revolutions_per_second_q16_16;
    uint32_t estimator_timestamp_us;
    uint32_t estimator_fault_flags;
    uint16_t alignment_zero_raw;
    int8_t alignment_direction;
    uint32_t electrical_phase_q32;
    uint32_t estimator_sample_interval_us;
    uint32_t estimator_maximum_sample_interval_us;
} command_encoder_status_t;

enum
{
    COMMAND_ENCODER_ESTIMATOR_READY = 1u << 0,
    COMMAND_ENCODER_ALIGNMENT_VALID = 1u << 1,
    COMMAND_ENCODER_ELECTRICAL_PHASE_VALID = 1u << 2
};

typedef struct
{
    uint8_t schema_version;
    uint16_t captured_sample_count;
    uint16_t sample_index;
    uint32_t loop_sample_count;
    int16_t current_a_reference_counts;
    int16_t current_b_reference_counts;
    int16_t current_a_measured_counts;
    int16_t current_b_measured_counts;
    int16_t phase_a_voltage_permille;
    int16_t phase_b_voltage_permille;
} command_current_trace_sample_t;

enum
{
    COMMAND_ALIGNMENT_FLAG_ACTIVE = 1u << 0,
    COMMAND_ALIGNMENT_FLAG_CALIBRATION_VALID = 1u << 1,
    COMMAND_ALIGNMENT_FLAG_AUTHORITY_ACTIVE = 1u << 2,
    COMMAND_ALIGNMENT_FLAG_BACKEND_ACTIVE = 1u << 3
};

typedef struct
{
    uint8_t schema_version;
    uint8_t state;
    uint8_t result;
    uint8_t flags;
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
    uint32_t remaining_millis;
    uint16_t minimum_current_counts;
    uint16_t maximum_current_counts;
    uint16_t expected_quarter_step_counts;
    uint16_t maximum_quarter_step_error_counts;
    uint32_t settle_duration_millis;
    uint32_t sample_duration_millis;
    uint32_t maximum_duration_millis;
    uint16_t minimum_sample_count;
    uint16_t maximum_sample_span_counts;
    uint16_t maximum_closure_error_counts;
    uint16_t maximum_current_error_counts;
} command_alignment_status_t;

enum
{
    COMMAND_CONFIGURATION_FLAG_STORE_INITIALIZED = 1u << 0,
    COMMAND_CONFIGURATION_FLAG_RECORD_VALID = 1u << 1,
    COMMAND_CONFIGURATION_FLAG_STORED_CALIBRATION_VALID = 1u << 2,
    COMMAND_CONFIGURATION_FLAG_ACTIVE_CALIBRATION_VALID = 1u << 3,
    COMMAND_CONFIGURATION_FLAG_ACTIVE_MATCHES_RECORD = 1u << 4,
    COMMAND_CONFIGURATION_FLAG_SLOT0_VALID = 1u << 5,
    COMMAND_CONFIGURATION_FLAG_SLOT1_VALID = 1u << 6,
    COMMAND_CONFIGURATION_FLAG_WRITE_SUPPORTED = 1u << 7
};

typedef struct
{
    uint8_t schema_version;
    uint8_t flags;
    uint8_t last_result;
    uint8_t active_slot;
    uint16_t record_schema_version;
    uint32_t generation;
    uint16_t stored_encoder_counts_per_revolution;
    uint16_t stored_electrical_cycles_per_revolution;
    uint16_t stored_electrical_zero_raw;
    uint16_t stored_observed_quarter_step_counts;
    int16_t stored_quarter_step_error_counts;
    int8_t stored_encoder_direction;
    uint16_t active_encoder_counts_per_revolution;
    uint16_t active_electrical_cycles_per_revolution;
    uint16_t active_electrical_zero_raw;
    uint16_t active_observed_quarter_step_counts;
    int16_t active_quarter_step_error_counts;
    int8_t active_encoder_direction;
} command_configuration_status_t;

enum
{
    COMMAND_TORQUE_FLAG_ACTIVE = 1u << 0,
    COMMAND_TORQUE_FLAG_AUTHORITY_ACTIVE = 1u << 1,
    COMMAND_TORQUE_FLAG_BACKEND_ACTIVE = 1u << 2,
    COMMAND_TORQUE_FLAG_ALIGNMENT_VALID = 1u << 3,
    COMMAND_TORQUE_FLAG_PHASE_VALID = 1u << 4,
    COMMAND_TORQUE_FLAG_DEMAND_AT_TARGET = 1u << 5
};

typedef struct
{
    uint8_t schema_version;
    uint8_t state;
    uint8_t result;
    uint8_t flags;
    uint32_t fault_flags;
    int16_t requested_q_current_counts;
    int16_t applied_q_current_counts;
    int16_t current_a_reference_counts;
    int16_t current_b_reference_counts;
    uint32_t electrical_phase_q32;
    int32_t velocity_revolutions_per_second_q16_16;
    int32_t acceleration_revolutions_per_second2_q16_16;
    uint32_t elapsed_millis;
    uint32_t remaining_millis;
    uint16_t maximum_current_counts;
    uint16_t maximum_current_slew_counts_per_second;
    int32_t maximum_velocity_revolutions_per_second_q16_16;
    int32_t maximum_acceleration_revolutions_per_second2_q16_16;
    uint16_t maximum_feedback_interval_us;
    uint32_t minimum_duration_millis;
    uint32_t maximum_duration_millis;
    uint32_t backend_fault_flags;
} command_aligned_torque_status_t;

enum
{
    COMMAND_VELOCITY_FLAG_ACTIVE = 1u << 0,
    COMMAND_VELOCITY_FLAG_AUTHORITY_ACTIVE = 1u << 1,
    COMMAND_VELOCITY_FLAG_BACKEND_ACTIVE = 1u << 2,
    COMMAND_VELOCITY_FLAG_ALIGNMENT_VALID = 1u << 3,
    COMMAND_VELOCITY_FLAG_ACTUATOR_ACTIVE = 1u << 4,
    COMMAND_VELOCITY_FLAG_REFERENCE_AT_TARGET = 1u << 5,
    COMMAND_VELOCITY_FLAG_CURRENT_AT_LIMIT = 1u << 6
};

typedef struct
{
    uint8_t schema_version;
    uint8_t state;
    uint8_t result;
    uint8_t flags;
    uint32_t fault_flags;
    int32_t target_velocity_revolutions_per_second_q16_16;
    int32_t reference_velocity_revolutions_per_second_q16_16;
    int32_t measured_velocity_revolutions_per_second_q16_16;
    int16_t requested_q_current_counts;
    int16_t applied_q_current_counts;
    uint16_t current_limit_counts;
    uint32_t elapsed_millis;
    uint32_t remaining_millis;
    int32_t maximum_target_velocity_revolutions_per_second_q16_16;
    int32_t maximum_target_acceleration_revolutions_per_second2_q16_16;
    int32_t maximum_feedback_velocity_revolutions_per_second_q16_16;
    uint16_t maximum_current_counts;
    uint16_t maximum_feedback_interval_us;
    int32_t proportional_gain_current_counts_per_velocity_q16_16;
    int32_t integral_gain_current_counts_per_position_q16_16;
    uint32_t maximum_duration_millis;
} command_velocity_status_t;

typedef command_status_t (*command_commissioning_get_status_fn)(
    void* context,
    command_commissioning_status_t* status);
typedef command_status_t (*command_commissioning_configure_fn)(
    void* context,
    const command_current_test_config_t* requested,
    command_current_test_config_t* applied);
typedef command_status_t (*command_commissioning_start_fn)(
    void* context,
    uint8_t selected_leg,
    uint32_t duration_millis);
typedef command_status_t (*command_commissioning_stop_fn)(void* context);
typedef command_status_t (*command_commissioning_get_boot_status_fn)(
    void* context,
    command_boot_status_t* status);
typedef command_status_t (*command_commissioning_get_encoder_status_fn)(
    void* context,
    command_encoder_status_t* status);
typedef command_status_t (*command_commissioning_get_current_trace_fn)(
    void* context,
    uint16_t sample_index,
    command_current_trace_sample_t* sample);
typedef command_status_t (*command_alignment_start_fn)(
    void* context,
    uint16_t alignment_current_counts);
typedef command_status_t (*command_alignment_get_status_fn)(
    void* context,
    command_alignment_status_t* status);
typedef command_status_t (*command_drive_stop_fn)(void* context);
typedef command_status_t (*command_configuration_get_status_fn)(
    void* context,
    command_configuration_status_t* status);
typedef command_status_t (*command_configuration_action_fn)(void* context);
typedef command_status_t (*command_aligned_torque_start_fn)(
    void* context,
    int16_t q_current_counts,
    uint32_t duration_millis);
typedef command_status_t (*command_aligned_torque_get_status_fn)(
    void* context,
    command_aligned_torque_status_t* status);
typedef command_status_t (*command_velocity_start_fn)(
    void* context,
    int32_t velocity_revolutions_per_second_q16_16,
    uint16_t current_limit_counts,
    uint32_t duration_millis);
typedef command_status_t (*command_velocity_get_status_fn)(
    void* context,
    command_velocity_status_t* status);

typedef struct
{
    void* context;
    command_commissioning_get_status_fn get_status;
    command_commissioning_configure_fn configure;
    command_commissioning_start_fn start;
    command_commissioning_stop_fn stop;
    command_commissioning_get_boot_status_fn get_boot_status;
    command_commissioning_get_encoder_status_fn get_encoder_status;
    command_commissioning_get_current_trace_fn get_current_trace;
} command_commissioning_api_t;

typedef struct
{
    void* context;
    command_alignment_start_fn start;
    command_alignment_get_status_fn get_status;
} command_alignment_api_t;

typedef struct
{
    void* context;
    command_drive_stop_fn stop;
} command_drive_api_t;

typedef struct
{
    void* context;
    command_configuration_get_status_fn get_status;
    command_configuration_action_fn save;
    command_configuration_action_fn clear_calibration;
} command_configuration_api_t;

typedef struct
{
    void* context;
    command_aligned_torque_start_fn start;
    command_aligned_torque_get_status_fn get_status;
} command_aligned_torque_api_t;

typedef struct
{
    void* context;
    command_velocity_start_fn start;
    command_velocity_get_status_fn get_status;
} command_velocity_api_t;

typedef struct
{
    uint32_t product_id;
    uint8_t firmware_major;
    uint8_t firmware_minor;
    uint16_t firmware_patch;
    uint8_t protocol_major;
    uint8_t protocol_minor;
    uint32_t capabilities;
    command_commissioning_api_t commissioning;
    command_alignment_api_t alignment;
    command_drive_api_t drive;
    command_configuration_api_t configuration;
    command_aligned_torque_api_t aligned_torque;
    command_velocity_api_t velocity;
} command_service_context_t;

typedef struct
{
    command_operation_t operation;
    const uint8_t* payload;
    size_t payload_length;
} command_request_t;

typedef enum
{
    COMMAND_RESPONSE_NONE = 0,
    COMMAND_RESPONSE_ECHO,
    COMMAND_RESPONSE_IDENTITY,
    COMMAND_RESPONSE_CAPABILITIES,
    COMMAND_RESPONSE_COMMISSIONING_STATUS,
    COMMAND_RESPONSE_CURRENT_TEST_CONFIG,
    COMMAND_RESPONSE_BOOT_STATUS,
    COMMAND_RESPONSE_ENCODER_STATUS,
    COMMAND_RESPONSE_CURRENT_TRACE,
    COMMAND_RESPONSE_ALIGNMENT_STATUS,
    COMMAND_RESPONSE_CONFIGURATION_STATUS,
    COMMAND_RESPONSE_ALIGNED_TORQUE_STATUS,
    COMMAND_RESPONSE_VELOCITY_STATUS
} command_response_kind_t;

typedef struct
{
    size_t length;
    uint8_t bytes[COMMAND_SERVICE_MAX_PING_BYTES];
} command_echo_response_t;

typedef struct
{
    uint32_t product_id;
    uint8_t firmware_major;
    uint8_t firmware_minor;
    uint16_t firmware_patch;
    uint8_t protocol_major;
    uint8_t protocol_minor;
} command_identity_response_t;

typedef struct
{
    command_status_t status;
    command_response_kind_t kind;
    union
    {
        command_echo_response_t echo;
        command_identity_response_t identity;
        uint32_t capabilities;
        command_commissioning_status_t commissioning_status;
        command_current_test_config_t current_test_config;
        command_boot_status_t boot_status;
        command_encoder_status_t encoder_status;
        command_current_trace_sample_t current_trace;
        command_alignment_status_t alignment_status;
        command_configuration_status_t configuration_status;
        command_aligned_torque_status_t aligned_torque_status;
        command_velocity_status_t velocity_status;
    } data;
} command_response_t;

void command_service_dispatch(const command_service_context_t* context,
                              const command_request_t* request,
                              command_response_t* response);

#endif
