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
    COMMAND_OPERATION_GET_CURRENT_TRACE
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
} command_encoder_status_t;

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
    uint32_t product_id;
    uint8_t firmware_major;
    uint8_t firmware_minor;
    uint16_t firmware_patch;
    uint8_t protocol_major;
    uint8_t protocol_minor;
    uint32_t capabilities;
    command_commissioning_api_t commissioning;
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
    COMMAND_RESPONSE_CURRENT_TRACE
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
    } data;
} command_response_t;

void command_service_dispatch(const command_service_context_t* context,
                              const command_request_t* request,
                              command_response_t* response);

#endif
