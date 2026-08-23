#include <stdbool.h>
#include <math.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mks57d/adc1.h"
#include "mks57d/adc_calibration.h"
#include "mks57d/adc_display.h"
#include "mks57d/aligned_torque_controller.h"
#include "mks57d/alignment_controller.h"
#include "mks57d/application_core.h"
#include "mks57d/angle_tracker.h"
#include "mks57d/app_state.h"
#include "mks57d/boot_self_test.h"
#include "mks57d/command_service.h"
#include "mks57d/configuration_store.h"
#include "mks57d/current_controller.h"
#include "mks57d/current_loop_backend.h"
#include "mks57d/diagnostics.h"
#include "mks57d/dma_channels.h"
#include "mks57d/dma_ring.h"
#include "mks57d/electrical_phase_predictor.h"
#include "mks57d/encoder_liveness.h"
#include "mks57d/encoder_display.h"
#include "mks57d/fault_latch.h"
#include "mks57d/interrupt_priority.h"
#include "mks57d/input_display.h"
#include "mks57d/mt6816.h"
#include "mks57d/motion_manager.h"
#include "mks57d/native_protocol.h"
#include "mks57d/motion_profile.h"
#include "mks57d/motor_alignment.h"
#include "mks57d/pi_controller.h"
#include "mks57d/phase_current_loop.h"
#include "mks57d/phase_current_reference.h"
#include "mks57d/position_controller.h"
#include "mks57d/rotating_current_test.h"
#include "mks57d/pulse_input_display.h"
#include "mks57d/servo_core.h"
#include "mks57d/ssd1306.h"
#include "mks57d/step_direction.h"
#include "mks57d/timebase_reconcile.h"
#include "mks57d/user_inputs.h"
#include "mks57d/velocity_controller.h"
#include "mks57d/watchdog_policy.h"

static unsigned int s_failures;

enum
{
    MOCK_I2C_MAX_CALLS = 32u,
    MOCK_I2C_MAX_BYTES = 32u,
    MOCK_SPI_MAX_BYTES = 8u,
    MOCK_PROTOCOL_CAPABILITIES = 0xA55Au,
    MOCK_CONFIGURATION_FLASH_WORDS_PER_SLOT =
        CONFIGURATION_STORE_PAGE_SIZE_BYTES / sizeof(uint32_t)
};

typedef struct
{
    size_t call_count;
    size_t fail_on_call;
    uint8_t addresses[MOCK_I2C_MAX_CALLS];
    size_t lengths[MOCK_I2C_MAX_CALLS];
    uint8_t bytes[MOCK_I2C_MAX_CALLS][MOCK_I2C_MAX_BYTES];
} mock_i2c_t;

typedef struct
{
    size_t call_count;
    size_t length;
    uint8_t transmit[MOCK_SPI_MAX_BYTES];
    uint8_t response[MOCK_SPI_MAX_BYTES];
    spi_status_t status;
} mock_spi_t;

typedef struct
{
    bool accept;
    size_t call_count;
    size_t length;
    uint8_t bytes[NATIVE_PROTOCOL_MAX_WIRE_FRAME_SIZE];
} mock_protocol_tx_t;

typedef struct
{
    uint32_t words[CONFIGURATION_STORE_SLOT_COUNT]
                  [MOCK_CONFIGURATION_FLASH_WORDS_PER_SLOT];
    size_t erase_calls;
    size_t program_calls;
    size_t fail_program_call;
    bool fail_erase;
} mock_configuration_flash_t;

typedef struct
{
    command_commissioning_status_t status;
    command_encoder_status_t encoder_status;
    command_current_trace_sample_t current_trace;
    command_alignment_status_t alignment_status;
    command_configuration_status_t configuration_status;
    command_aligned_torque_status_t aligned_torque_status;
    command_velocity_status_t velocity_status;
    command_position_status_t position_status;
    command_fault_recovery_status_t fault_recovery_status;
    command_current_test_config_t requested_config;
    uint8_t requested_leg;
    uint32_t requested_duration_millis;
    size_t status_calls;
    size_t configure_calls;
    size_t start_calls;
    size_t stop_calls;
    size_t boot_status_calls;
    size_t encoder_status_calls;
    size_t current_trace_calls;
    size_t alignment_start_calls;
    size_t alignment_status_calls;
    size_t drive_stop_calls;
    size_t drive_clear_faults_calls;
    size_t configuration_status_calls;
    size_t configuration_save_calls;
    size_t calibration_clear_calls;
    size_t aligned_torque_start_calls;
    size_t aligned_torque_status_calls;
    size_t velocity_start_calls;
    size_t velocity_status_calls;
    size_t position_start_calls;
    size_t position_status_calls;
    uint16_t requested_trace_index;
    uint16_t requested_alignment_current_counts;
    int16_t requested_q_current_counts;
    uint32_t requested_torque_duration_millis;
    int32_t requested_velocity_revolutions_per_second_q16_16;
    uint16_t requested_velocity_current_limit_counts;
    uint32_t requested_velocity_duration_millis;
    int32_t requested_position_displacement_revolutions_q16_16;
    int32_t requested_position_maximum_velocity_q16_16;
    int32_t requested_position_maximum_acceleration_q16_16;
    uint16_t requested_position_current_limit_counts;
    uint32_t requested_position_duration_millis;
} mock_commissioning_t;

static servo_core_config_t test_servo_config(void)
{
    const servo_core_config_t config = {
        .motion_profile = {
            .maximum_velocity_revolutions_per_second = 2.0f,
            .maximum_acceleration_revolutions_per_second_squared = 4.0f,
            .maximum_step_seconds = 0.002f,
            .position_tolerance_revolutions = 0.0005f,
            .velocity_tolerance_revolutions_per_second = 0.002f,
        },
        .velocity_controller = {
            .proportional_gain = 2.0f,
            .integral_gain_per_second = 8.0f,
            .output_limit = 2.0f,
            .integrator_limit = 2.0f,
        },
        .maximum_feedback_interval_us = 2000u,
        .feedback_stale_timeout_us = 3000u,
        .maximum_control_interval_us = 2000u,
        .maximum_feedback_velocity_revolutions_per_second = 20.0f,
        .position_gain_per_second = 8.0f,
        .maximum_following_error_revolutions = 0.5f,
        .maximum_current_amperes = 2.0f,
    };

    return config;
}

static velocity_controller_config_t test_velocity_controller_config(void)
{
    const velocity_controller_config_t config = {
        .current_controller = {
            .proportional_gain = 100.0f,
            .integral_gain_per_second = 200.0f,
            .output_limit = 100.0f,
            .integrator_limit = 100.0f,
        },
        .maximum_target_velocity_revolutions_per_second = 1.0f,
        .maximum_target_acceleration_revolutions_per_second_squared = 1.0f,
        .maximum_feedback_velocity_revolutions_per_second = 5.0f,
        .maximum_current_counts = 100u,
        .maximum_feedback_interval_us = 2000u,
        .minimum_duration_millis = 3u,
        .maximum_duration_millis = INT32_MAX,
    };

    return config;
}

static position_controller_config_t test_position_controller_config(void)
{
    const position_controller_config_t config = {
        .maximum_relative_travel_revolutions = 100.0f,
        .maximum_velocity_revolutions_per_second = 4.0f,
        .maximum_velocity_target_revolutions_per_second = 5.0f,
        .maximum_acceleration_revolutions_per_second_squared = 4.0f,
        .maximum_feedback_velocity_revolutions_per_second = 6.0f,
        .maximum_start_velocity_revolutions_per_second = 0.1f,
        .maximum_following_error_revolutions = 0.25f,
        .position_gain_per_second = 4.0f,
        .position_tolerance_revolutions = 0.002f,
        .velocity_tolerance_revolutions_per_second = 0.02f,
        .maximum_current_counts = 100u,
        .required_settle_samples = 5u,
        .maximum_feedback_interval_us = 2000u,
        .minimum_duration_millis = 100u,
        .maximum_duration_millis = INT32_MAX,
    };

    return config;
}

static application_core_config_t test_application_config(
    uint32_t lease_timeout_us,
    uint32_t allowed_motion_sources)
{
    const application_core_config_t config = {
        .servo = {
            .motion_profile = {
                .maximum_velocity_revolutions_per_second = 2.0f,
                .maximum_acceleration_revolutions_per_second_squared = 4.0f,
                .maximum_step_seconds = 0.002f,
                .position_tolerance_revolutions = 0.0005f,
                .velocity_tolerance_revolutions_per_second = 0.002f,
            },
            .velocity_controller = {
                .proportional_gain = 2.0f,
                .integral_gain_per_second = 8.0f,
                .output_limit = 2.0f,
                .integrator_limit = 2.0f,
            },
            .maximum_feedback_interval_us = 2000u,
            .feedback_stale_timeout_us = 3000u,
            .maximum_control_interval_us = 2000u,
            .maximum_feedback_velocity_revolutions_per_second = 20.0f,
            .position_gain_per_second = 8.0f,
            .maximum_following_error_revolutions = 0.5f,
            .maximum_current_amperes = 2.0f,
        },
        .motion = {
            .remote_lease_timeout_us = lease_timeout_us,
            .allowed_motion_sources = allowed_motion_sources,
        },
        .step_direction = {
            .steps_per_revolution = 3200u,
            .maximum_sample_interval_us = 2000u,
            .maximum_step_rate_per_second = 160000.0f,
        },
    };

    return config;
}

static servo_core_status_t observe_servo(
    servo_core_t* core,
    float position_revolutions,
    float velocity_revolutions_per_second,
    uint32_t timestamp_us)
{
    const rotor_observation_t observation = {
        .position_revolutions = position_revolutions,
        .velocity_revolutions_per_second =
            velocity_revolutions_per_second,
        .timestamp_us = timestamp_us,
        .valid = true,
    };

    return servo_core_observe_rotor(core, &observation);
}

static application_core_status_t observe_application(
    application_core_t* application,
    float position_revolutions,
    float velocity_revolutions_per_second,
    uint32_t timestamp_us)
{
    const rotor_observation_t observation = {
        .position_revolutions = position_revolutions,
        .velocity_revolutions_per_second =
            velocity_revolutions_per_second,
        .timestamp_us = timestamp_us,
        .valid = true,
    };

    return application_core_observe_rotor(application, &observation);
}

#define EXPECT_TRUE(expression)                                                     \
    do                                                                              \
    {                                                                               \
        if (!(expression))                                                          \
        {                                                                           \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression);          \
            ++s_failures;                                                           \
        }                                                                           \
    } while (false)

static i2c_status_t mock_i2c_write(void* context,
                                   uint8_t address_7bit,
                                   const uint8_t* bytes,
                                   size_t length)
{
    mock_i2c_t* mock = context;
    const size_t call = mock->call_count;
    size_t index;

    if ((call >= MOCK_I2C_MAX_CALLS) ||
        (length > MOCK_I2C_MAX_BYTES))
    {
        return I2C_STATUS_INVALID_ARGUMENT;
    }

    mock->addresses[call] = address_7bit;
    mock->lengths[call] = length;
    for (index = 0u; index < length; ++index)
    {
        mock->bytes[call][index] = bytes[index];
    }
    ++mock->call_count;

    if ((mock->fail_on_call != 0u) &&
        (mock->call_count == mock->fail_on_call))
    {
        return I2C_STATUS_DATA_NACK;
    }
    return I2C_STATUS_OK;
}

static spi_status_t mock_spi_exchange(void* context,
                                      const uint8_t* transmit,
                                      uint8_t* receive,
                                      size_t length)
{
    mock_spi_t* mock = context;
    size_t index;

    if ((transmit == NULL) || (receive == NULL) ||
        (length > MOCK_SPI_MAX_BYTES))
    {
        return SPI_STATUS_INVALID_ARGUMENT;
    }

    ++mock->call_count;
    mock->length = length;
    for (index = 0u; index < length; ++index)
    {
        mock->transmit[index] = transmit[index];
        receive[index] = mock->response[index];
    }
    return mock->status;
}

static bool mock_protocol_send(void* context,
                               const uint8_t* bytes,
                               size_t length)
{
    mock_protocol_tx_t* mock = context;
    size_t index;

    ++mock->call_count;
    if ((length > sizeof(mock->bytes)) || !mock->accept)
    {
        return false;
    }
    mock->length = length;
    for (index = 0u; index < length; ++index)
    {
        mock->bytes[index] = bytes[index];
    }
    return true;
}

static void mock_configuration_flash_init(mock_configuration_flash_t* flash)
{
    uint8_t slot;
    size_t word;

    memset(flash, 0, sizeof(*flash));
    for (slot = 0u; slot < CONFIGURATION_STORE_SLOT_COUNT; ++slot)
    {
        for (word = 0u;
             word < MOCK_CONFIGURATION_FLASH_WORDS_PER_SLOT;
             ++word)
        {
            flash->words[slot][word] = UINT32_MAX;
        }
    }
}

static bool mock_configuration_read_word(void* context,
                                         uint8_t slot,
                                         size_t word_index,
                                         uint32_t* value)
{
    mock_configuration_flash_t* flash = context;

    if ((flash == NULL) || (value == NULL) ||
        (slot >= CONFIGURATION_STORE_SLOT_COUNT) ||
        (word_index >= MOCK_CONFIGURATION_FLASH_WORDS_PER_SLOT))
    {
        return false;
    }
    *value = flash->words[slot][word_index];
    return true;
}

static bool mock_configuration_erase_slot(void* context, uint8_t slot)
{
    mock_configuration_flash_t* flash = context;
    size_t word;

    if ((flash == NULL) || (slot >= CONFIGURATION_STORE_SLOT_COUNT) ||
        flash->fail_erase)
    {
        return false;
    }
    ++flash->erase_calls;
    for (word = 0u;
         word < MOCK_CONFIGURATION_FLASH_WORDS_PER_SLOT;
         ++word)
    {
        flash->words[slot][word] = UINT32_MAX;
    }
    return true;
}

static bool mock_configuration_program_word(void* context,
                                             uint8_t slot,
                                             size_t word_index,
                                             uint32_t value)
{
    mock_configuration_flash_t* flash = context;

    if ((flash == NULL) || (slot >= CONFIGURATION_STORE_SLOT_COUNT) ||
        (word_index >= MOCK_CONFIGURATION_FLASH_WORDS_PER_SLOT))
    {
        return false;
    }
    ++flash->program_calls;
    if ((flash->fail_program_call != 0u) &&
        (flash->program_calls == flash->fail_program_call))
    {
        return false;
    }
    if (flash->words[slot][word_index] != UINT32_MAX)
    {
        return false;
    }
    flash->words[slot][word_index] = value;
    return true;
}

static configuration_store_backend_t mock_configuration_backend(
    mock_configuration_flash_t* flash)
{
    const configuration_store_backend_t backend = {
        .context = flash,
        .read_word = mock_configuration_read_word,
        .erase_slot = mock_configuration_erase_slot,
        .program_word = mock_configuration_program_word,
    };

    return backend;
}

static command_status_t mock_commissioning_get_status(
    void* context,
    command_commissioning_status_t* status)
{
    mock_commissioning_t* mock = context;

    ++mock->status_calls;
    *status = mock->status;
    return COMMAND_STATUS_OK;
}

static command_status_t mock_commissioning_configure(
    void* context,
    const command_current_test_config_t* requested,
    command_current_test_config_t* applied)
{
    mock_commissioning_t* mock = context;

    ++mock->configure_calls;
    mock->requested_config = *requested;
    *applied = *requested;
    return COMMAND_STATUS_OK;
}

static command_status_t mock_commissioning_start(
    void* context,
    uint8_t selected_leg,
    uint32_t duration_millis)
{
    mock_commissioning_t* mock = context;

    ++mock->start_calls;
    mock->requested_leg = selected_leg;
    mock->requested_duration_millis = duration_millis;
    return COMMAND_STATUS_OK;
}

static command_status_t mock_commissioning_stop(void* context)
{
    mock_commissioning_t* mock = context;

    ++mock->stop_calls;
    return COMMAND_STATUS_OK;
}

static command_status_t mock_commissioning_get_boot_status(
    void* context,
    command_boot_status_t* status)
{
    mock_commissioning_t* mock = context;

    ++mock->boot_status_calls;
    status->schema_version = 1u;
    status->reset_flags = 0x28000000u;
    status->retained_panic = 15u;
    status->uptime_millis = 0x01020304u;
    return COMMAND_STATUS_OK;
}

static command_status_t mock_commissioning_get_encoder_status(
    void* context,
    command_encoder_status_t* status)
{
    mock_commissioning_t* mock = context;

    ++mock->encoder_status_calls;
    *status = mock->encoder_status;
    return COMMAND_STATUS_OK;
}

static command_status_t mock_commissioning_get_current_trace(
    void* context,
    uint16_t sample_index,
    command_current_trace_sample_t* sample)
{
    mock_commissioning_t* mock = context;

    ++mock->current_trace_calls;
    mock->requested_trace_index = sample_index;
    *sample = mock->current_trace;
    return COMMAND_STATUS_OK;
}

static command_status_t mock_alignment_start(
    void* context,
    uint16_t alignment_current_counts)
{
    mock_commissioning_t* mock = context;

    ++mock->alignment_start_calls;
    mock->requested_alignment_current_counts =
        alignment_current_counts;
    return COMMAND_STATUS_OK;
}

static command_status_t mock_alignment_get_status(
    void* context,
    command_alignment_status_t* status)
{
    mock_commissioning_t* mock = context;

    ++mock->alignment_status_calls;
    *status = mock->alignment_status;
    return COMMAND_STATUS_OK;
}

static command_status_t mock_drive_stop(void* context)
{
    mock_commissioning_t* mock = context;

    ++mock->drive_stop_calls;
    return COMMAND_STATUS_OK;
}

static command_status_t mock_drive_clear_faults(
    void* context,
    command_fault_recovery_status_t* status)
{
    mock_commissioning_t* mock = context;

    ++mock->drive_clear_faults_calls;
    *status = mock->fault_recovery_status;
    return COMMAND_STATUS_OK;
}

static command_status_t mock_configuration_get_status(
    void* context,
    command_configuration_status_t* status)
{
    mock_commissioning_t* mock = context;

    ++mock->configuration_status_calls;
    *status = mock->configuration_status;
    return COMMAND_STATUS_OK;
}

static command_status_t mock_configuration_save(void* context)
{
    mock_commissioning_t* mock = context;

    ++mock->configuration_save_calls;
    return COMMAND_STATUS_OK;
}

static command_status_t mock_calibration_clear(void* context)
{
    mock_commissioning_t* mock = context;

    ++mock->calibration_clear_calls;
    return COMMAND_STATUS_OK;
}

static command_status_t mock_aligned_torque_start(
    void* context,
    int16_t q_current_counts,
    uint32_t duration_millis)
{
    mock_commissioning_t* mock = context;

    ++mock->aligned_torque_start_calls;
    mock->requested_q_current_counts = q_current_counts;
    mock->requested_torque_duration_millis = duration_millis;
    return COMMAND_STATUS_OK;
}

static command_status_t mock_aligned_torque_get_status(
    void* context,
    command_aligned_torque_status_t* status)
{
    mock_commissioning_t* mock = context;

    ++mock->aligned_torque_status_calls;
    *status = mock->aligned_torque_status;
    return COMMAND_STATUS_OK;
}

static command_status_t mock_velocity_start(
    void* context,
    int32_t velocity_revolutions_per_second_q16_16,
    uint16_t current_limit_counts,
    uint32_t duration_millis)
{
    mock_commissioning_t* mock = context;

    ++mock->velocity_start_calls;
    mock->requested_velocity_revolutions_per_second_q16_16 =
        velocity_revolutions_per_second_q16_16;
    mock->requested_velocity_current_limit_counts = current_limit_counts;
    mock->requested_velocity_duration_millis = duration_millis;
    return COMMAND_STATUS_OK;
}

static command_status_t mock_velocity_get_status(
    void* context,
    command_velocity_status_t* status)
{
    mock_commissioning_t* mock = context;

    ++mock->velocity_status_calls;
    *status = mock->velocity_status;
    return COMMAND_STATUS_OK;
}

static command_status_t mock_position_start_relative(
    void* context,
    int32_t displacement_revolutions_q16_16,
    int32_t maximum_velocity_revolutions_per_second_q16_16,
    int32_t maximum_acceleration_revolutions_per_second2_q16_16,
    uint16_t current_limit_counts,
    uint32_t duration_millis)
{
    mock_commissioning_t* mock = context;

    ++mock->position_start_calls;
    mock->requested_position_displacement_revolutions_q16_16 =
        displacement_revolutions_q16_16;
    mock->requested_position_maximum_velocity_q16_16 =
        maximum_velocity_revolutions_per_second_q16_16;
    mock->requested_position_maximum_acceleration_q16_16 =
        maximum_acceleration_revolutions_per_second2_q16_16;
    mock->requested_position_current_limit_counts = current_limit_counts;
    mock->requested_position_duration_millis = duration_millis;
    return COMMAND_STATUS_OK;
}

static command_status_t mock_position_get_status(
    void* context,
    command_position_status_t* status)
{
    mock_commissioning_t* mock = context;

    ++mock->position_status_calls;
    *status = mock->position_status;
    return COMMAND_STATUS_OK;
}

static bool init_native_server(native_protocol_server_t* server,
                               mock_protocol_tx_t* transmit)
{
    const command_service_context_t context = {
        .product_id = COMMAND_SERVICE_PRODUCT_ID_MKS57D,
        .firmware_major = 0u,
        .firmware_minor = 4u,
        .firmware_patch = 0u,
        .protocol_major = NATIVE_PROTOCOL_VERSION_MAJOR,
        .protocol_minor = NATIVE_PROTOCOL_VERSION_MINOR,
        .capabilities = MOCK_PROTOCOL_CAPABILITIES,
    };

    return native_protocol_server_init(
        server,
        NATIVE_PROTOCOL_DEFAULT_DEVICE_ADDRESS,
        &context,
        mock_protocol_send,
        transmit);
}

static bool init_commissioning_server(native_protocol_server_t* server,
                                      mock_protocol_tx_t* transmit,
                                      mock_commissioning_t* commissioning)
{
    const command_service_context_t context = {
        .product_id = COMMAND_SERVICE_PRODUCT_ID_MKS57D,
        .protocol_major = NATIVE_PROTOCOL_VERSION_MAJOR,
        .protocol_minor = NATIVE_PROTOCOL_VERSION_MINOR,
        .commissioning = {
            .context = commissioning,
            .get_status = mock_commissioning_get_status,
            .configure = mock_commissioning_configure,
            .start = mock_commissioning_start,
            .stop = mock_commissioning_stop,
            .get_boot_status = mock_commissioning_get_boot_status,
            .get_encoder_status = mock_commissioning_get_encoder_status,
            .get_current_trace = mock_commissioning_get_current_trace,
        },
        .alignment = {
            .context = commissioning,
            .start = mock_alignment_start,
            .get_status = mock_alignment_get_status,
        },
        .drive = {
            .context = commissioning,
            .stop = mock_drive_stop,
            .clear_faults = mock_drive_clear_faults,
        },
        .configuration = {
            .context = commissioning,
            .get_status = mock_configuration_get_status,
            .save = mock_configuration_save,
            .clear_calibration = mock_calibration_clear,
        },
        .aligned_torque = {
            .context = commissioning,
            .start = mock_aligned_torque_start,
            .get_status = mock_aligned_torque_get_status,
        },
        .velocity = {
            .context = commissioning,
            .start = mock_velocity_start,
            .get_status = mock_velocity_get_status,
        },
        .position = {
            .context = commissioning,
            .start_relative = mock_position_start_relative,
            .get_status = mock_position_get_status,
        },
    };

    return native_protocol_server_init(
        server,
        NATIVE_PROTOCOL_DEFAULT_DEVICE_ADDRESS,
        &context,
        mock_protocol_send,
        transmit);
}

static size_t encode_native_request(uint8_t address,
                                    uint16_t sequence,
                                    uint8_t message_type,
                                    uint16_t command,
                                    const uint8_t* payload,
                                    size_t payload_length,
                                    uint8_t* wire,
                                    size_t capacity)
{
    native_protocol_frame_t frame = {
        .version = NATIVE_PROTOCOL_VERSION_MAJOR,
        .device_address = address,
        .sequence = sequence,
        .message_type = message_type,
        .command = command,
        .payload_length = payload_length,
    };
    size_t index;

    if ((payload == NULL) && (payload_length != 0u))
    {
        return 0u;
    }
    for (index = 0u; index < payload_length; ++index)
    {
        frame.payload[index] = payload[index];
    }
    return native_protocol_encode_wire_frame(&frame, wire, capacity);
}

static void test_reset_only_enters_diagnostic_after_passive_init(void)
{
    app_supervisor_t supervisor;
    const app_transition_context_t unsafe = {0};

    EXPECT_TRUE(app_supervisor_init(&supervisor));
    EXPECT_TRUE(supervisor.state == APP_STATE_RESET_SAFE);
    EXPECT_TRUE(supervisor.authority == APP_AUTHORITY_NONE);
    EXPECT_TRUE(!app_supervisor_foreground_service_allowed(&supervisor));
    EXPECT_TRUE(!app_supervisor_handle_event(&supervisor,
                                             APP_EVENT_FAULT_ACKNOWLEDGED,
                                             unsafe));
    EXPECT_TRUE(app_supervisor_handle_event(
        &supervisor, APP_EVENT_PASSIVE_INIT_COMPLETE, unsafe));
    EXPECT_TRUE(supervisor.state == APP_STATE_DIAGNOSTIC);
    EXPECT_TRUE(app_supervisor_foreground_service_allowed(&supervisor));
}

static void test_faults_converge_on_fault_state(void)
{
    app_supervisor_t supervisor;
    const app_transition_context_t safe = {.safe_to_energize = true};

    EXPECT_TRUE(app_supervisor_init(&supervisor));
    EXPECT_TRUE(app_supervisor_handle_event(
        &supervisor, APP_EVENT_PASSIVE_INIT_COMPLETE, safe));
    EXPECT_TRUE(app_supervisor_handle_event(
        &supervisor, APP_EVENT_READINESS_CONFIRMED, safe));
    EXPECT_TRUE(app_supervisor_handle_event(
        &supervisor, APP_EVENT_MOTION_RUN_REQUESTED, safe));
    EXPECT_TRUE(app_supervisor_bridge_authorized(&supervisor));
    EXPECT_TRUE(app_supervisor_handle_event(
        &supervisor, APP_EVENT_FAULT_DETECTED, safe));
    EXPECT_TRUE(supervisor.state == APP_STATE_FAULT);
    EXPECT_TRUE(supervisor.authority == APP_AUTHORITY_NONE);
    EXPECT_TRUE(!app_supervisor_bridge_authorized(&supervisor));
    EXPECT_TRUE(app_supervisor_foreground_service_allowed(&supervisor));
}

static void test_fault_recovery_requires_explicit_safe_context(void)
{
    app_supervisor_t supervisor;
    const app_transition_context_t unsafe = {0};
    const app_transition_context_t safe = {.safe_to_recover = true};

    EXPECT_TRUE(app_supervisor_init(&supervisor));
    EXPECT_TRUE(app_supervisor_handle_event(
        &supervisor, APP_EVENT_FAULT_DETECTED, unsafe));
    EXPECT_TRUE(!app_supervisor_handle_event(
        &supervisor, APP_EVENT_FAULT_ACKNOWLEDGED, unsafe));
    EXPECT_TRUE(supervisor.state == APP_STATE_FAULT);
    EXPECT_TRUE(app_supervisor_handle_event(
        &supervisor, APP_EVENT_FAULT_ACKNOWLEDGED, safe));
    EXPECT_TRUE(supervisor.state == APP_STATE_DIAGNOSTIC);
}

static void test_drive_supervisor_owns_diagnostic_and_motion_authority(void)
{
    app_supervisor_t supervisor;
    const app_transition_context_t unsafe = {0};
    const app_transition_context_t safe = {.safe_to_energize = true};

    EXPECT_TRUE(app_supervisor_init(&supervisor));
    EXPECT_TRUE(app_supervisor_handle_event(
        &supervisor, APP_EVENT_PASSIVE_INIT_COMPLETE, unsafe));
    EXPECT_TRUE(!app_supervisor_handle_event(
        &supervisor, APP_EVENT_READINESS_CONFIRMED, unsafe));
    EXPECT_TRUE(supervisor.state == APP_STATE_DIAGNOSTIC);
    EXPECT_TRUE(app_supervisor_handle_event(
        &supervisor, APP_EVENT_READINESS_CONFIRMED, safe));
    EXPECT_TRUE(supervisor.state == APP_STATE_READY);

    EXPECT_TRUE(app_supervisor_handle_event(
        &supervisor, APP_EVENT_DIAGNOSTIC_OPERATION_REQUESTED, safe));
    EXPECT_TRUE(supervisor.state == APP_STATE_RUN);
    EXPECT_TRUE(supervisor.authority == APP_AUTHORITY_DIAGNOSTIC);
    EXPECT_TRUE(app_supervisor_bridge_authorized(&supervisor));
    EXPECT_TRUE(app_supervisor_handle_event(
        &supervisor, APP_EVENT_AUTHORITY_RELEASED, unsafe));
    EXPECT_TRUE(supervisor.state == APP_STATE_READY);
    EXPECT_TRUE(!app_supervisor_bridge_authorized(&supervisor));

    EXPECT_TRUE(app_supervisor_handle_event(
        &supervisor, APP_EVENT_ALIGNMENT_REQUESTED, safe));
    EXPECT_TRUE(supervisor.state == APP_STATE_ALIGN);
    EXPECT_TRUE(supervisor.authority == APP_AUTHORITY_MOTION);
    EXPECT_TRUE(app_supervisor_bridge_authorized(&supervisor));
    EXPECT_TRUE(app_supervisor_handle_event(
        &supervisor, APP_EVENT_ALIGNMENT_COMPLETED, unsafe));
    EXPECT_TRUE(supervisor.state == APP_STATE_READY);
    EXPECT_TRUE(supervisor.authority == APP_AUTHORITY_NONE);
}

static void test_readiness_loss_deauthorizes_or_faults(void)
{
    app_supervisor_t supervisor;
    const app_transition_context_t safe = {.safe_to_energize = true};

    EXPECT_TRUE(app_supervisor_init(&supervisor));
    EXPECT_TRUE(app_supervisor_handle_event(
        &supervisor, APP_EVENT_PASSIVE_INIT_COMPLETE, safe));
    EXPECT_TRUE(app_supervisor_handle_event(
        &supervisor, APP_EVENT_READINESS_CONFIRMED, safe));
    EXPECT_TRUE(app_supervisor_handle_event(
        &supervisor, APP_EVENT_READINESS_LOST, safe));
    EXPECT_TRUE(supervisor.state == APP_STATE_DIAGNOSTIC);
    EXPECT_TRUE(supervisor.authority == APP_AUTHORITY_NONE);

    EXPECT_TRUE(app_supervisor_handle_event(
        &supervisor, APP_EVENT_READINESS_CONFIRMED, safe));
    EXPECT_TRUE(app_supervisor_handle_event(
        &supervisor, APP_EVENT_DIAGNOSTIC_OPERATION_REQUESTED, safe));
    EXPECT_TRUE(app_supervisor_handle_event(
        &supervisor, APP_EVENT_READINESS_LOST, safe));
    EXPECT_TRUE(supervisor.state == APP_STATE_FAULT);
    EXPECT_TRUE(supervisor.authority == APP_AUTHORITY_NONE);
    EXPECT_TRUE(!app_supervisor_bridge_authorized(&supervisor));
}

static void test_drive_supervisor_rejects_state_authority_mismatch(void)
{
    app_supervisor_t supervisor;
    const app_transition_context_t safe = {.safe_to_energize = true};

    EXPECT_TRUE(app_supervisor_init(&supervisor));
    EXPECT_TRUE(app_supervisor_handle_event(
        &supervisor, APP_EVENT_PASSIVE_INIT_COMPLETE, safe));
    EXPECT_TRUE(app_supervisor_handle_event(
        &supervisor, APP_EVENT_READINESS_CONFIRMED, safe));
    supervisor.authority = APP_AUTHORITY_MOTION;

    EXPECT_TRUE(!app_supervisor_bridge_authorized(&supervisor));
    EXPECT_TRUE(!app_supervisor_foreground_service_allowed(&supervisor));
    EXPECT_TRUE(!app_supervisor_handle_event(
        &supervisor, APP_EVENT_MOTION_RUN_REQUESTED, safe));
    EXPECT_TRUE(supervisor.state == APP_STATE_FAULT);
    EXPECT_TRUE(supervisor.authority == APP_AUTHORITY_NONE);
}

static void test_fault_latch_preserves_first_fault_and_accumulates_flags(void)
{
    fault_latch_t latch;

    fault_latch_init(&latch);
    EXPECT_TRUE(!fault_latch_is_active(&latch));
    EXPECT_TRUE(latch.first == FAULT_SOURCE_NONE);

    fault_latch_raise(&latch, FAULT_SOURCE_CLOCK);
    fault_latch_raise(&latch, FAULT_SOURCE_CORE_EXCEPTION);

    EXPECT_TRUE(fault_latch_is_active(&latch));
    EXPECT_TRUE(latch.first == FAULT_SOURCE_CLOCK);
    EXPECT_TRUE((latch.active & FAULT_SOURCE_CLOCK) != 0u);
    EXPECT_TRUE((latch.active & FAULT_SOURCE_CORE_EXCEPTION) != 0u);
}

static void test_watchdog_policy_services_only_on_schedule(void)
{
    watchdog_policy_t policy;

    watchdog_policy_init(&policy, 0u);
    EXPECT_TRUE(watchdog_policy_step(&policy, 99u, true) ==
                WATCHDOG_POLICY_ACTION_NONE);
    EXPECT_TRUE(watchdog_policy_step(&policy, 100u, true) ==
                WATCHDOG_POLICY_ACTION_FEED);
    EXPECT_TRUE(watchdog_policy_step(&policy, 199u, true) ==
                WATCHDOG_POLICY_ACTION_NONE);
    EXPECT_TRUE(watchdog_policy_step(&policy, 200u, true) ==
                WATCHDOG_POLICY_ACTION_FEED);
}

static void test_watchdog_policy_latches_failed_health(void)
{
    watchdog_policy_t policy;

    watchdog_policy_init(&policy, 0u);
    EXPECT_TRUE(watchdog_policy_step(&policy, 1u, false) ==
                WATCHDOG_POLICY_ACTION_FAIL);
    EXPECT_TRUE(watchdog_policy_step(&policy, 2u, true) ==
                WATCHDOG_POLICY_ACTION_FAIL);
}

static void test_watchdog_policy_rejects_foreground_deadline_miss(void)
{
    watchdog_policy_t policy;

    watchdog_policy_init(&policy, 0u);
    EXPECT_TRUE(watchdog_policy_step(
                    &policy,
                    WATCHDOG_FOREGROUND_DEADLINE_MS + 1u,
                    true) == WATCHDOG_POLICY_ACTION_FAIL);
}

static void test_watchdog_policy_handles_millisecond_wrap(void)
{
    watchdog_policy_t policy;
    const uint32_t start = UINT32_MAX - 50u;

    watchdog_policy_init(&policy, start);
    EXPECT_TRUE(watchdog_policy_step(&policy, 10u, true) ==
                WATCHDOG_POLICY_ACTION_NONE);
    EXPECT_TRUE(watchdog_policy_step(&policy, 49u, true) ==
                WATCHDOG_POLICY_ACTION_FEED);
}

static void test_diagnostics_record_abi(void)
{
    volatile uint32_t magic = DIAGNOSTICS_RECORD_MAGIC;
    volatile uint32_t schema = DIAGNOSTICS_RECORD_SCHEMA_VERSION;
    volatile size_t record_size = sizeof(diagnostics_record_t);
    volatile size_t sequence_offset = offsetof(diagnostics_record_t, sequence);
    volatile size_t panic_offset = offsetof(diagnostics_record_t, retained_panic);
    volatile size_t encoder_offset =
        offsetof(diagnostics_record_t, encoder_status);
    volatile size_t rs485_offset =
        offsetof(diagnostics_record_t, rs485_status);
    volatile size_t protocol_offset =
        offsetof(diagnostics_record_t, native_protocol_ready);
    volatile size_t current_loop_offset =
        offsetof(diagnostics_record_t, current_loop_ready);
    volatile uint32_t capabilities = DIAGNOSTICS_CAPABILITIES_CURRENT;

    EXPECT_TRUE(magic == 0x4D4B5335u);
    EXPECT_TRUE(schema == 5u);
    EXPECT_TRUE(record_size == 240u);
    EXPECT_TRUE(sequence_offset == 12u);
    EXPECT_TRUE(panic_offset == 48u);
    EXPECT_TRUE(encoder_offset == 64u);
    EXPECT_TRUE(rs485_offset == 92u);
    EXPECT_TRUE(protocol_offset == 136u);
    EXPECT_TRUE(current_loop_offset == 184u);
    EXPECT_TRUE((capabilities &
                 DIAGNOSTICS_CAPABILITY_NATIVE_PROTOCOL) != 0u);
    EXPECT_TRUE((capabilities &
                 DIAGNOSTICS_CAPABILITY_DISPLAY_I2C) != 0u);
    EXPECT_TRUE((capabilities &
                 DIAGNOSTICS_CAPABILITY_PASSIVE_ADC) != 0u);
    EXPECT_TRUE((capabilities &
                 DIAGNOSTICS_CAPABILITY_USER_INPUTS) != 0u);
    EXPECT_TRUE((capabilities &
                 DIAGNOSTICS_CAPABILITY_CURRENT_LOOP) != 0u);
    EXPECT_TRUE((capabilities &
                 DIAGNOSTICS_CAPABILITY_CURRENT_DIAGNOSTIC) != 0u);
    EXPECT_TRUE((capabilities &
                 DIAGNOSTICS_CAPABILITY_PRODUCT_IMAGE) != 0u);
    EXPECT_TRUE((capabilities &
                 DIAGNOSTICS_CAPABILITY_ALIGNMENT) != 0u);
    EXPECT_TRUE((capabilities &
                 DIAGNOSTICS_CAPABILITY_VELOCITY_CONTROL) != 0u);
    EXPECT_TRUE((capabilities &
                 DIAGNOSTICS_CAPABILITY_FAULT_RECOVERY) != 0u);
}

static void test_dma_channel_budget_contract(void)
{
    volatile unsigned int adc = DMA_CHANNEL_ADC_CURRENT;
    volatile unsigned int encoder_rx = DMA_CHANNEL_ENCODER_RX;
    volatile unsigned int encoder_tx = DMA_CHANNEL_ENCODER_TX;
    volatile unsigned int usart_rx = DMA_CHANNEL_USART1_RX;
    volatile unsigned int usart_tx = DMA_CHANNEL_USART1_TX;
    volatile unsigned int pwm = DMA_CHANNEL_TIM3_BURST;

    EXPECT_TRUE(adc == 1u);
    EXPECT_TRUE(encoder_rx == 2u);
    EXPECT_TRUE(encoder_tx == 3u);
    EXPECT_TRUE(usart_rx == 4u);
    EXPECT_TRUE(usart_tx == 5u);
    EXPECT_TRUE(pwm == 6u);
}

static void test_dma_ring_copies_across_wrap(void)
{
    const uint8_t ring[8] = {8u, 9u, 2u, 3u, 4u, 5u, 6u, 7u};
    uint8_t received[4] = {0u};
    dma_ring_cursor_t cursor;

    dma_ring_cursor_init(&cursor, 6u);
    EXPECT_TRUE(dma_ring_copy(&cursor,
                              ring,
                              sizeof(ring),
                              10u,
                              received,
                              sizeof(received)) == 4u);
    EXPECT_TRUE(received[0] == 6u);
    EXPECT_TRUE(received[1] == 7u);
    EXPECT_TRUE(received[2] == 8u);
    EXPECT_TRUE(received[3] == 9u);
    EXPECT_TRUE(cursor.consumed_total == 10u);
    EXPECT_TRUE(cursor.overrun_count == 0u);
    EXPECT_TRUE(cursor.dropped_bytes == 0u);
}

static void test_dma_ring_accounts_overwrite(void)
{
    const uint8_t ring[8] = {8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u};
    uint8_t received[8] = {0u};
    dma_ring_cursor_t cursor;

    dma_ring_cursor_init(&cursor, 0u);
    EXPECT_TRUE(dma_ring_copy(&cursor,
                              ring,
                              sizeof(ring),
                              16u,
                              received,
                              sizeof(received)) == 8u);
    EXPECT_TRUE(received[0] == 8u);
    EXPECT_TRUE(received[7] == 15u);
    EXPECT_TRUE(cursor.consumed_total == 16u);
    EXPECT_TRUE(cursor.overrun_count == 1u);
    EXPECT_TRUE(cursor.dropped_bytes == 8u);
}

static void test_dma_ring_handles_counter_wrap(void)
{
    const uint8_t ring[8] = {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u};
    uint8_t received[4] = {0u};
    dma_ring_cursor_t cursor;

    dma_ring_cursor_init(&cursor, UINT32_MAX - 1u);
    EXPECT_TRUE(dma_ring_copy(&cursor,
                              ring,
                              sizeof(ring),
                              2u,
                              received,
                              sizeof(received)) == 4u);
    EXPECT_TRUE(received[0] == 6u);
    EXPECT_TRUE(received[1] == 7u);
    EXPECT_TRUE(received[2] == 0u);
    EXPECT_TRUE(received[3] == 1u);
    EXPECT_TRUE(cursor.consumed_total == 2u);
}

static void test_mt6816_decodes_angle_and_even_parity(void)
{
    mt6816_sample_t sample = {0};

    EXPECT_TRUE(mt6816_decode_registers(0xA9u,
                                        0x55u,
                                        0x00u,
                                        &sample) == MT6816_STATUS_OK);
    EXPECT_TRUE(sample.angle_raw == 0x2A55u);
    EXPECT_TRUE(sample.flags == 0u);
    EXPECT_TRUE(sample.register_03 == 0xA9u);
    EXPECT_TRUE(sample.register_04 == 0x55u);
    EXPECT_TRUE(sample.register_05 == 0x00u);
}

static void test_mt6816_reports_sensor_warning_flags(void)
{
    mt6816_sample_t sample = {0};

    EXPECT_TRUE(mt6816_decode_registers(0x00u,
                                        0x03u,
                                        0x08u,
                                        &sample) == MT6816_STATUS_OK);
    EXPECT_TRUE(sample.angle_raw == 0u);
    EXPECT_TRUE((sample.flags & MT6816_FLAG_NO_MAGNET) != 0u);
    EXPECT_TRUE((sample.flags & MT6816_FLAG_OVER_SPEED) != 0u);
}

static void test_mt6816_rejects_bad_parity_without_publishing(void)
{
    mt6816_sample_t sample = {
        .angle_raw = 1234u,
        .flags = 0x5Au,
    };

    EXPECT_TRUE(mt6816_decode_registers(0x00u,
                                        0x01u,
                                        0x00u,
                                        &sample) ==
                MT6816_STATUS_PARITY_ERROR);
    EXPECT_TRUE(sample.angle_raw == 1234u);
    EXPECT_TRUE(sample.flags == 0x5Au);
}

static void test_mt6816_uses_one_coherent_burst(void)
{
    mock_spi_t mock = {
        .response = {0xFFu, 0xA9u, 0x55u, 0x08u},
        .status = SPI_STATUS_OK,
    };
    const spi_bus_t bus = {
        .exchange = mock_spi_exchange,
        .context = &mock,
    };
    mt6816_sample_t sample = {0};
    spi_status_t transport_status = SPI_STATUS_NOT_READY;

    EXPECT_TRUE(mt6816_read_angle(&bus,
                                  &sample,
                                  &transport_status) == MT6816_STATUS_OK);
    EXPECT_TRUE(transport_status == SPI_STATUS_OK);
    EXPECT_TRUE(mock.call_count == 1u);
    EXPECT_TRUE(mock.length == 4u);
    EXPECT_TRUE(mock.transmit[0] == 0x83u);
    EXPECT_TRUE(mock.transmit[1] == 0x00u);
    EXPECT_TRUE(mock.transmit[2] == 0x00u);
    EXPECT_TRUE(mock.transmit[3] == 0x00u);
    EXPECT_TRUE(sample.angle_raw == 0x2A55u);
    EXPECT_TRUE((sample.flags & MT6816_FLAG_OVER_SPEED) != 0u);
}

static void test_mt6816_preserves_transport_failure(void)
{
    mock_spi_t mock = {.status = SPI_STATUS_RECEIVE_TIMEOUT};
    const spi_bus_t bus = {
        .exchange = mock_spi_exchange,
        .context = &mock,
    };
    mt6816_sample_t sample = {.angle_raw = 77u};
    spi_status_t transport_status = SPI_STATUS_OK;

    EXPECT_TRUE(mt6816_read_angle(&bus,
                                  &sample,
                                  &transport_status) ==
                MT6816_STATUS_TRANSPORT_ERROR);
    EXPECT_TRUE(transport_status == SPI_STATUS_RECEIVE_TIMEOUT);
    EXPECT_TRUE(sample.angle_raw == 77u);
}

static void test_boot_self_test_requires_every_gate(void)
{
    boot_self_test_t self_test;

    boot_self_test_init(&self_test, BOOT_SELF_TEST_REQUIRED_PASSIVE);
    EXPECT_TRUE(!boot_self_test_ready(&self_test));

    boot_self_test_pass(&self_test,
                        BOOT_SELF_TEST_REQUIRED_PASSIVE &
                            ~BOOT_SELF_TEST_WATCHDOG);
    EXPECT_TRUE(!boot_self_test_ready(&self_test));

    boot_self_test_pass(&self_test, BOOT_SELF_TEST_WATCHDOG);
    EXPECT_TRUE(boot_self_test_ready(&self_test));
}

static void test_boot_self_test_failure_is_latched(void)
{
    boot_self_test_t self_test;

    boot_self_test_init(&self_test, BOOT_SELF_TEST_REQUIRED_PASSIVE);
    boot_self_test_pass(&self_test, BOOT_SELF_TEST_REQUIRED_PASSIVE);
    boot_self_test_fail(&self_test, BOOT_SELF_TEST_PASSIVE_BOARD);

    EXPECT_TRUE(!boot_self_test_ready(&self_test));
    EXPECT_TRUE((self_test.failed & BOOT_SELF_TEST_PASSIVE_BOARD) != 0u);
    EXPECT_TRUE((self_test.passed & BOOT_SELF_TEST_PASSIVE_BOARD) == 0u);

    boot_self_test_pass(&self_test, BOOT_SELF_TEST_PASSIVE_BOARD);
    EXPECT_TRUE(!boot_self_test_ready(&self_test));
    EXPECT_TRUE((self_test.passed & BOOT_SELF_TEST_PASSIVE_BOARD) == 0u);
}

static void test_interrupt_priority_contract(void)
{
    volatile unsigned int grouping =
        INTERRUPT_PRIORITY_GROUP_ALL_PREEMPT;
    volatile unsigned int emergency = INTERRUPT_PRIORITY_EMERGENCY_FAULT;
    volatile unsigned int guardian = INTERRUPT_PRIORITY_CONTROL_GUARDIAN;
    volatile unsigned int current = INTERRUPT_PRIORITY_FAST_CURRENT;
    volatile unsigned int feedback = INTERRUPT_PRIORITY_ROTOR_FEEDBACK;
    volatile unsigned int communications =
        INTERRUPT_PRIORITY_COMMUNICATIONS;
    volatile unsigned int timekeeping = INTERRUPT_PRIORITY_TIMEKEEPING;

    EXPECT_TRUE(grouping == 3u);
    EXPECT_TRUE(emergency < guardian);
    EXPECT_TRUE(guardian < current);
    EXPECT_TRUE(current < feedback);
    EXPECT_TRUE(feedback < communications);
    EXPECT_TRUE(timekeeping == 15u);
}

static void test_timebase_reconciles_preempted_systick_epoch(void)
{
    const uint32_t observation_us = 1000424u;
    const uint32_t raw_now_us = 1000000u;
    const uint32_t corrected_now_us =
        timebase_reconcile_microseconds(
            observation_us, raw_now_us, 1000u);

    EXPECT_TRUE(corrected_now_us == 1001000u);
    EXPECT_TRUE((corrected_now_us - observation_us) == 576u);
    EXPECT_TRUE(timebase_reconcile_microseconds(
                    1000999u, 1000000u, 1000u) == 1001000u);
}

static void test_timebase_reconciliation_clamps_stale_samples(void)
{
    EXPECT_TRUE(timebase_reconcile_microseconds(
                    5000u, 3000u, 1000u) == 5000u);
    EXPECT_TRUE(timebase_reconcile_microseconds(
                    5000u, 4000u, 1000u) == 5000u);
    EXPECT_TRUE(timebase_reconcile_microseconds(
                    5000u, 5000u, 1000u) == 5000u);
    EXPECT_TRUE(timebase_reconcile_microseconds(
                    5000u, 5100u, 1000u) == 5100u);
}

static void test_timebase_reconciliation_preserves_uint32_wrap(void)
{
    const uint32_t before_wrap = UINT32_MAX - 15u;
    const uint32_t after_wrap = 32u;
    const uint32_t missing_epoch_sample = before_wrap - 424u;

    EXPECT_TRUE(timebase_reconcile_microseconds(
                    before_wrap, after_wrap, 1000u) == after_wrap);
    EXPECT_TRUE(timebase_reconcile_microseconds(
                    before_wrap,
                    missing_epoch_sample,
                    1000u) == 560u);
}

static void test_adc_channel_and_sample_order_contract(void)
{
    adc_sample_t sample;
    volatile unsigned int current_b_channel = ADC1_CURRENT_B_CHANNEL;
    volatile unsigned int current_a_channel = ADC1_CURRENT_A_CHANNEL;
    volatile unsigned int vbus_channel = ADC1_VBUS_CHANNEL;
    volatile unsigned int max_clock_hz = ADC1_MAX_CLOCK_HZ;

    EXPECT_TRUE(current_b_channel == 2u);
    EXPECT_TRUE(current_a_channel == 3u);
    EXPECT_TRUE(vbus_channel == 4u);
    EXPECT_TRUE(max_clock_hz == 16000000u);

    EXPECT_TRUE(adc_sample_build(&sample,
                                 101u,
                                 202u,
                                 303u,
                                 17u));
    EXPECT_TRUE(sample.current_b_raw == 101u);
    EXPECT_TRUE(sample.current_a_raw == 202u);
    EXPECT_TRUE(sample.vbus_raw == 303u);
    EXPECT_TRUE(sample.capture_index == 17u);
    EXPECT_TRUE(adc_sample_is_valid(&sample));
}

static void test_adc_sample_rejects_values_outside_12_bits(void)
{
    adc_sample_t sample = {
        .current_b_raw = 11u,
        .current_a_raw = 22u,
        .vbus_raw = 33u,
        .capture_index = 44u,
    };

    EXPECT_TRUE(adc_sample_build(&sample,
                                 ADC_SAMPLE_RAW_MAX,
                                 ADC_SAMPLE_RAW_MAX,
                                 ADC_SAMPLE_RAW_MAX,
                                 UINT32_MAX));
    EXPECT_TRUE(adc_sample_is_valid(&sample));

    EXPECT_TRUE(!adc_sample_build(&sample, 4096u, 2u, 3u, 5u));
    EXPECT_TRUE(sample.current_b_raw == ADC_SAMPLE_RAW_MAX);
    EXPECT_TRUE(sample.current_a_raw == ADC_SAMPLE_RAW_MAX);
    EXPECT_TRUE(sample.vbus_raw == ADC_SAMPLE_RAW_MAX);
    EXPECT_TRUE(sample.capture_index == UINT32_MAX);

    sample.vbus_raw = 4096u;
    EXPECT_TRUE(!adc_sample_is_valid(&sample));
    EXPECT_TRUE(!adc_sample_is_valid(NULL));
    EXPECT_TRUE(!adc_sample_build(NULL, 1u, 2u, 3u, 4u));
}

static void test_adc_calibration_uses_measured_front_end_scaling(void)
{
    adc_calibration_t calibration;
    adc_engineering_sample_t engineering = {
        .current_b_amperes = 10.0f,
        .current_a_amperes = 20.0f,
        .vbus_volts = 30.0f,
        .capture_index = 40u,
    };
    adc_sample_t raw;
    const float expected_amperes_per_count =
        (ADC_NOMINAL_REFERENCE_VOLTS / (float)ADC_SAMPLE_RAW_MAX) /
        (ADC_CURRENT_SHUNT_OHMS * ADC_CURRENT_SENSE_GAIN);
    const float expected_vbus =
        895.0f *
        (ADC_NOMINAL_REFERENCE_VOLTS / (float)ADC_SAMPLE_RAW_MAX) *
        ((ADC_VBUS_UPPER_RESISTANCE_OHMS +
          ADC_VBUS_LOWER_RESISTANCE_OHMS) /
         ADC_VBUS_LOWER_RESISTANCE_OHMS);

    EXPECT_TRUE(!adc_calibration_build(NULL, 3.3f, 2053.0f, 2041.0f));
    EXPECT_TRUE(!adc_calibration_build(&calibration,
                                       0.0f,
                                       2053.0f,
                                       2041.0f));
    EXPECT_TRUE(!adc_calibration_build(&calibration,
                                       3.3f,
                                       4096.0f,
                                       2041.0f));
    EXPECT_TRUE(adc_calibration_build(&calibration,
                                      ADC_NOMINAL_REFERENCE_VOLTS,
                                      2053.0f,
                                      2041.0f));
    EXPECT_TRUE(adc_calibration_is_valid(&calibration));

    EXPECT_TRUE(adc_sample_build(&raw, 2053u, 2041u, 895u, 23u));
    EXPECT_TRUE(adc_sample_convert(&raw, &calibration, &engineering));
    EXPECT_TRUE(fabsf(engineering.current_b_amperes) < 0.000001f);
    EXPECT_TRUE(fabsf(engineering.current_a_amperes) < 0.000001f);
    EXPECT_TRUE(fabsf(engineering.vbus_volts - expected_vbus) < 0.00001f);
    EXPECT_TRUE(engineering.capture_index == 23u);

    EXPECT_TRUE(adc_sample_build(&raw, 2052u, 2042u, 895u, 24u));
    EXPECT_TRUE(adc_sample_convert(&raw, &calibration, &engineering));
    EXPECT_TRUE(fabsf(engineering.current_b_amperes +
                      expected_amperes_per_count) < 0.000001f);
    EXPECT_TRUE(fabsf(engineering.current_a_amperes -
                      expected_amperes_per_count) < 0.000001f);
    EXPECT_TRUE(fabsf(expected_amperes_per_count - 0.00606f) < 0.00001f);

    raw.vbus_raw = ADC_SAMPLE_RAW_MAX + 1u;
    EXPECT_TRUE(!adc_sample_convert(&raw, &calibration, &engineering));
    EXPECT_TRUE(engineering.capture_index == 24u);
    EXPECT_TRUE(!adc_sample_convert(NULL, &calibration, &engineering));
    EXPECT_TRUE(!adc_sample_convert(&raw, NULL, &engineering));
    EXPECT_TRUE(!adc_sample_convert(&raw, &calibration, NULL));
}

static void test_adc_zero_calibration_and_milliamp_conversion(void)
{
    adc_zero_calibrator_t calibrator;
    adc_calibration_t calibration;
    int32_t current_b_milliamperes = 123;
    int32_t current_a_milliamperes = 456;
    uint32_t sample;

    EXPECT_TRUE(!adc_zero_calibrator_init(NULL,
                                          ADC_NOMINAL_REFERENCE_VOLTS));
    EXPECT_TRUE(!adc_zero_calibrator_init(&calibrator, 0.0f));
    EXPECT_TRUE(adc_zero_calibrator_init(
        &calibrator,
        ADC_NOMINAL_REFERENCE_VOLTS));
    EXPECT_TRUE(!adc_zero_calibrator_get(&calibrator, &calibration));

    for (sample = 0u; sample < ADC_ZERO_CALIBRATION_SAMPLE_COUNT; ++sample)
    {
        EXPECT_TRUE(adc_zero_calibrator_observe(
            &calibrator,
            (uint16_t)(2052u + (sample & 1u)),
            (uint16_t)(2041u + (sample & 1u))));
    }
    EXPECT_TRUE(adc_zero_calibrator_get(&calibrator, &calibration));
    EXPECT_TRUE(fabsf(calibration.current_b_zero_raw - 2052.5f) <
                0.000001f);
    EXPECT_TRUE(fabsf(calibration.current_a_zero_raw - 2041.5f) <
                0.000001f);

    EXPECT_TRUE(adc_current_pair_convert_milliamperes(
        2054u,
        2040u,
        &calibration,
        &current_b_milliamperes,
        &current_a_milliamperes));
    EXPECT_TRUE(current_b_milliamperes == 9);
    EXPECT_TRUE(current_a_milliamperes == -9);
    EXPECT_TRUE(!adc_current_pair_convert_milliamperes(
        2054u,
        2040u,
        NULL,
        &current_b_milliamperes,
        &current_a_milliamperes));
}

static void test_servo57d_oled_profile_is_valid(void)
{
    const ssd1306_panel_config_t* config =
        &SSD1306_PANEL_SERVO57D;

    EXPECT_TRUE(ssd1306_config_is_valid(config));
    EXPECT_TRUE(config->address_7bit == 0x3Cu);
    EXPECT_TRUE(config->width == 72u);
    EXPECT_TRUE(config->height == 40u);
    EXPECT_TRUE(config->column_offset == 28u);
    EXPECT_TRUE(config->multiplex_ratio == 0x27u);
    EXPECT_TRUE(config->start_line == 0u);
}

static bool encoder_display_pixel_is_set(const uint8_t* pixels,
                                         size_t x,
                                         size_t y)
{
    const size_t index =
        ((y / 8u) * ENCODER_DISPLAY_WIDTH) + x;

    return (pixels[index] & (uint8_t)(1u << (y % 8u))) != 0u;
}

static void test_adc_display_labels_channels_and_rejects_invalid_values(void)
{
    uint8_t current_a[ADC_DISPLAY_FRAME_BYTES];
    uint8_t current_b[ADC_DISPLAY_FRAME_BYTES];
    uint8_t vbus[ADC_DISPLAY_FRAME_BYTES];
    uint8_t fault[ADC_DISPLAY_FRAME_BYTES];
    uint8_t invalid[ADC_DISPLAY_FRAME_BYTES];
    uint8_t out_of_range[ADC_DISPLAY_FRAME_BYTES];

    EXPECT_TRUE(!adc_display_render(NULL,
                                    sizeof(current_a),
                                    ADC_DISPLAY_CURRENT_A,
                                    0u,
                                    true));
    EXPECT_TRUE(!adc_display_render(current_a,
                                    sizeof(current_a) - 1u,
                                    ADC_DISPLAY_CURRENT_A,
                                    0u,
                                    true));
    EXPECT_TRUE(!adc_display_render(current_a,
                                    sizeof(current_a),
                                    ADC_DISPLAY_CHANNEL_COUNT,
                                    0u,
                                    true));
    EXPECT_TRUE(adc_display_render(current_a,
                                   sizeof(current_a),
                                   ADC_DISPLAY_CURRENT_A,
                                   2048u,
                                   true));
    EXPECT_TRUE(adc_display_render(current_b,
                                   sizeof(current_b),
                                   ADC_DISPLAY_CURRENT_B,
                                   2048u,
                                   true));
    EXPECT_TRUE(adc_display_render(vbus,
                                   sizeof(vbus),
                                   ADC_DISPLAY_VBUS,
                                   ADC_SAMPLE_RAW_MAX,
                                   true));
    EXPECT_TRUE(adc_display_render(fault,
                                   sizeof(fault),
                                   ADC_DISPLAY_FAULT,
                                   19u,
                                   true));
    EXPECT_TRUE(adc_display_render(invalid,
                                   sizeof(invalid),
                                   ADC_DISPLAY_CURRENT_A,
                                   0u,
                                   false));
    EXPECT_TRUE(adc_display_render(out_of_range,
                                   sizeof(out_of_range),
                                   ADC_DISPLAY_CURRENT_A,
                                   ADC_SAMPLE_RAW_MAX + 1u,
                                   true));

    EXPECT_TRUE(memcmp(current_a, current_b, sizeof(current_a)) != 0);
    EXPECT_TRUE(memcmp(current_a, vbus, sizeof(current_a)) != 0);
    EXPECT_TRUE(memcmp(current_a, fault, sizeof(current_a)) != 0);
    EXPECT_TRUE(memcmp(vbus, fault, sizeof(vbus)) != 0);
    EXPECT_TRUE(memcmp(invalid, out_of_range, sizeof(invalid)) == 0);
}

static void test_adc_display_renders_both_signed_milliamp_values(void)
{
    uint8_t values[ADC_DISPLAY_FRAME_BYTES];
    uint8_t reversed[ADC_DISPLAY_FRAME_BYTES];
    uint8_t invalid[ADC_DISPLAY_FRAME_BYTES];

    EXPECT_TRUE(!adc_display_render_currents_milliamperes(
        NULL, sizeof(values), 0, 0, true));
    EXPECT_TRUE(!adc_display_render_currents_milliamperes(
        values, sizeof(values) - 1u, 0, 0, true));
    EXPECT_TRUE(adc_display_render_currents_milliamperes(
        values, sizeof(values), 1234, -5678, true));
    EXPECT_TRUE(adc_display_render_currents_milliamperes(
        reversed, sizeof(reversed), -5678, 1234, true));
    EXPECT_TRUE(adc_display_render_currents_milliamperes(
        invalid, sizeof(invalid), 0, 0, false));
    EXPECT_TRUE(memcmp(values, reversed, sizeof(values)) != 0);
    EXPECT_TRUE(memcmp(values, invalid, sizeof(values)) != 0);
}

static void test_encoder_display_renders_position_and_invalid_state(void)
{
    uint8_t zero[ENCODER_DISPLAY_FRAME_BYTES];
    uint8_t maximum[ENCODER_DISPLAY_FRAME_BYTES];
    uint8_t invalid[ENCODER_DISPLAY_FRAME_BYTES];
    uint8_t out_of_range[ENCODER_DISPLAY_FRAME_BYTES];

    EXPECT_TRUE(!encoder_display_render(NULL, sizeof(zero), 0u, true));
    EXPECT_TRUE(!encoder_display_render(zero, sizeof(zero) - 1u, 0u, true));
    EXPECT_TRUE(encoder_display_render(zero, sizeof(zero), 0u, true));
    EXPECT_TRUE(encoder_display_render(maximum,
                                       sizeof(maximum),
                                       MT6816_ANGLE_RAW_MAX,
                                       true));
    EXPECT_TRUE(encoder_display_render(invalid,
                                       sizeof(invalid),
                                       0u,
                                       false));
    EXPECT_TRUE(encoder_display_render(out_of_range,
                                       sizeof(out_of_range),
                                       MT6816_ANGLE_RAW_MAX + 1u,
                                       true));

    EXPECT_TRUE(memcmp(zero, maximum, sizeof(zero)) != 0);
    EXPECT_TRUE(memcmp(invalid, out_of_range, sizeof(invalid)) == 0);
    EXPECT_TRUE(encoder_display_pixel_is_set(zero, 7u, 3u));
    EXPECT_TRUE(!encoder_display_pixel_is_set(invalid, 7u, 3u));
    EXPECT_TRUE(encoder_display_pixel_is_set(invalid, 7u, 7u));
}

static void test_user_inputs_debounce_each_active_low_signal_independently(void)
{
    user_inputs_debouncer_t debouncer = {0};
    uint32_t raw = USER_INPUT_MASK;

    EXPECT_TRUE(!user_inputs_debouncer_init(NULL, raw));
    EXPECT_TRUE(!user_inputs_debouncer_init(
        &debouncer,
        USER_INPUT_MASK | (1u << 12)));
    EXPECT_TRUE(user_inputs_debouncer_init(&debouncer, raw));
    EXPECT_TRUE(user_inputs_debounced_levels(&debouncer) == USER_INPUT_MASK);

    raw &= ~((uint32_t)USER_INPUT_BUTTON_CENTER);
    EXPECT_TRUE(!user_inputs_debouncer_update(&debouncer, raw));
    EXPECT_TRUE(!user_inputs_debouncer_update(&debouncer, raw));
    EXPECT_TRUE(user_inputs_debounced_levels(&debouncer) == USER_INPUT_MASK);
    EXPECT_TRUE(user_inputs_debouncer_update(&debouncer, raw));
    EXPECT_TRUE((user_inputs_debounced_levels(&debouncer) &
                 USER_INPUT_BUTTON_CENTER) == 0u);

    raw &= ~((uint32_t)USER_INPUT_M_IN1);
    EXPECT_TRUE(!user_inputs_debouncer_update(&debouncer, raw));
    raw |= USER_INPUT_M_IN1;
    EXPECT_TRUE(!user_inputs_debouncer_update(&debouncer, raw));
    raw &= ~((uint32_t)USER_INPUT_M_IN1);
    EXPECT_TRUE(!user_inputs_debouncer_update(&debouncer, raw));
    EXPECT_TRUE(!user_inputs_debouncer_update(&debouncer, raw));
    EXPECT_TRUE(user_inputs_debouncer_update(&debouncer, raw));
    EXPECT_TRUE((user_inputs_debounced_levels(&debouncer) &
                 USER_INPUT_M_IN1) == 0u);
    EXPECT_TRUE((user_inputs_debounced_levels(&debouncer) &
                 USER_INPUT_BUTTON_RIGHT) != 0u);

    raw &= ~((uint32_t)USER_INPUT_STEP);
    EXPECT_TRUE(!user_inputs_debouncer_update(&debouncer, raw));
    EXPECT_TRUE(!user_inputs_debouncer_update(&debouncer, raw));
    EXPECT_TRUE(user_inputs_debouncer_update(&debouncer, raw));
    EXPECT_TRUE((user_inputs_debounced_levels(&debouncer) &
                 USER_INPUT_STEP) == 0u);
}

static void test_input_display_labels_five_raw_levels(void)
{
    uint8_t all_high[INPUT_DISPLAY_FRAME_BYTES];
    uint8_t all_low[INPUT_DISPLAY_FRAME_BYTES];
    uint8_t invalid[INPUT_DISPLAY_FRAME_BYTES];

    EXPECT_TRUE(!input_display_render(NULL,
                                      sizeof(all_high),
                                      USER_INPUT_MASK,
                                      true));
    EXPECT_TRUE(!input_display_render(all_high,
                                      sizeof(all_high) - 1u,
                                      USER_INPUT_MASK,
                                      true));
    EXPECT_TRUE(!input_display_render(all_high,
                                      sizeof(all_high),
                                      USER_INPUT_MASK | (1u << 12),
                                      true));
    EXPECT_TRUE(input_display_render(all_high,
                                     sizeof(all_high),
                                     USER_INPUT_MASK,
                                     true));
    EXPECT_TRUE(input_display_render(all_low,
                                     sizeof(all_low),
                                     0u,
                                     true));
    EXPECT_TRUE(input_display_render(invalid,
                                     sizeof(invalid),
                                     USER_INPUT_MASK,
                                     false));
    EXPECT_TRUE(memcmp(all_high, all_low, sizeof(all_high)) != 0);
    EXPECT_TRUE(memcmp(all_high, invalid, sizeof(all_high)) != 0);
    EXPECT_TRUE(memcmp(all_low, invalid, sizeof(all_low)) != 0);
}

static void test_pulse_input_display_labels_three_raw_levels(void)
{
    uint8_t all_high[PULSE_INPUT_DISPLAY_FRAME_BYTES];
    uint8_t all_low[PULSE_INPUT_DISPLAY_FRAME_BYTES];
    uint8_t invalid[PULSE_INPUT_DISPLAY_FRAME_BYTES];

    EXPECT_TRUE(!pulse_input_display_render(NULL,
                                            sizeof(all_high),
                                            USER_INPUT_MASK,
                                            true));
    EXPECT_TRUE(!pulse_input_display_render(all_high,
                                            sizeof(all_high) - 1u,
                                            USER_INPUT_MASK,
                                            true));
    EXPECT_TRUE(!pulse_input_display_render(all_high,
                                            sizeof(all_high),
                                            USER_INPUT_MASK | (1u << 12),
                                            true));
    EXPECT_TRUE(pulse_input_display_render(all_high,
                                           sizeof(all_high),
                                           USER_INPUT_MASK,
                                           true));
    EXPECT_TRUE(pulse_input_display_render(all_low,
                                           sizeof(all_low),
                                           0u,
                                           true));
    EXPECT_TRUE(pulse_input_display_render(invalid,
                                           sizeof(invalid),
                                           USER_INPUT_MASK,
                                           false));
    EXPECT_TRUE(memcmp(all_high, all_low, sizeof(all_high)) != 0);
    EXPECT_TRUE(memcmp(all_high, invalid, sizeof(all_high)) != 0);
    EXPECT_TRUE(memcmp(all_low, invalid, sizeof(all_low)) != 0);
}

static void test_ssd1306_init_uses_one_bounded_command_transaction(void)
{
    mock_i2c_t mock = {0};
    const i2c_bus_t bus = {
        .write = mock_i2c_write,
        .context = &mock,
    };

    EXPECT_TRUE(ssd1306_initialize(
                    &bus,
                    &SSD1306_PANEL_SERVO57D) == I2C_STATUS_OK);
    EXPECT_TRUE(mock.call_count == 1u);
    EXPECT_TRUE(mock.addresses[0] == 0x3Cu);
    EXPECT_TRUE(mock.lengths[0] <= MOCK_I2C_MAX_BYTES);
    EXPECT_TRUE(mock.bytes[0][0] == 0x00u);
    EXPECT_TRUE(mock.bytes[0][1] == 0xAEu);
    EXPECT_TRUE(mock.bytes[0][mock.lengths[0] - 1u] == 0xAFu);
}

static void test_ssd1306_frame_uses_configured_visible_window(void)
{
    uint8_t pixels[72u * 5u];
    mock_i2c_t mock = {0};
    const i2c_bus_t bus = {
        .write = mock_i2c_write,
        .context = &mock,
    };
    size_t index;

    for (index = 0u; index < sizeof(pixels); ++index)
    {
        pixels[index] = (uint8_t)index;
    }

    EXPECT_TRUE(ssd1306_write_frame(
                    &bus,
                    &SSD1306_PANEL_SERVO57D,
                    pixels,
                    sizeof(pixels)) == I2C_STATUS_OK);
    EXPECT_TRUE(mock.call_count == 13u);
    EXPECT_TRUE(mock.lengths[0] == 7u);
    EXPECT_TRUE(mock.bytes[0][0] == 0x00u);
    EXPECT_TRUE(mock.bytes[0][1] == 0x21u);
    EXPECT_TRUE(mock.bytes[0][2] == 28u);
    EXPECT_TRUE(mock.bytes[0][3] == 99u);
    EXPECT_TRUE(mock.bytes[0][4] == 0x22u);
    EXPECT_TRUE(mock.bytes[0][5] == 0u);
    EXPECT_TRUE(mock.bytes[0][6] == 4u);
    EXPECT_TRUE(mock.bytes[1][0] == 0x40u);
    EXPECT_TRUE(mock.lengths[12] == 20u);
}

static void test_ssd1306_partial_pages_use_requested_window(void)
{
    uint8_t pixels[ENCODER_DISPLAY_FRAME_BYTES] = {0};
    mock_i2c_t mock = {0};
    const i2c_bus_t bus = {
        .write = mock_i2c_write,
        .context = &mock,
    };

    EXPECT_TRUE(ssd1306_write_pages(
                    &bus,
                    &SSD1306_PANEL_SERVO57D,
                    ENCODER_DISPLAY_START_PAGE,
                    ENCODER_DISPLAY_PAGE_COUNT,
                    pixels,
                    sizeof(pixels)) == I2C_STATUS_OK);
    EXPECT_TRUE(mock.call_count == 6u);
    EXPECT_TRUE(mock.bytes[0][4] == 0x22u);
    EXPECT_TRUE(mock.bytes[0][5] == 1u);
    EXPECT_TRUE(mock.bytes[0][6] == 2u);
    EXPECT_TRUE(mock.lengths[1] == 32u);
    EXPECT_TRUE(mock.lengths[5] == 21u);
    EXPECT_TRUE(ssd1306_write_pages(
                    &bus,
                    &SSD1306_PANEL_SERVO57D,
                    4u,
                    2u,
                    pixels,
                    sizeof(pixels)) == I2C_STATUS_INVALID_ARGUMENT);
}

static void test_ssd1306_stops_after_transport_failure(void)
{
    uint8_t pixels[72u * 5u] = {0};
    mock_i2c_t mock = {.fail_on_call = 2u};
    const i2c_bus_t bus = {
        .write = mock_i2c_write,
        .context = &mock,
    };

    EXPECT_TRUE(ssd1306_write_frame(
                    &bus,
                    &SSD1306_PANEL_SERVO57D,
                    pixels,
                    sizeof(pixels)) == I2C_STATUS_DATA_NACK);
    EXPECT_TRUE(mock.call_count == 2u);
}

static void test_native_protocol_crc_matches_standard_vector(void)
{
    static const uint8_t vector[] = {
        '1', '2', '3', '4', '5', '6', '7', '8', '9'
    };

    EXPECT_TRUE(native_protocol_crc16_ccitt_false(
                    vector,
                    sizeof(vector)) == 0x29B1u);
}

static void test_native_protocol_codec_accepts_maximum_payload(void)
{
    uint8_t wire[NATIVE_PROTOCOL_MAX_WIRE_FRAME_SIZE];
    native_protocol_frame_t source = {
        .version = NATIVE_PROTOCOL_VERSION_MAJOR,
        .device_address = NATIVE_PROTOCOL_DEFAULT_DEVICE_ADDRESS,
        .sequence = 0xFFFFu,
        .message_type = NATIVE_PROTOCOL_MESSAGE_REQUEST,
        .command = NATIVE_PROTOCOL_COMMAND_PING,
        .payload_length = NATIVE_PROTOCOL_MAX_PAYLOAD_SIZE,
    };
    native_protocol_frame_t decoded;
    size_t wire_length;
    size_t index;

    for (index = 0u; index < source.payload_length; ++index)
    {
        source.payload[index] = (uint8_t)index;
    }
    wire_length = native_protocol_encode_wire_frame(
        &source,
        wire,
        sizeof(wire));
    EXPECT_TRUE(wire_length != 0u);
    EXPECT_TRUE(wire_length <= NATIVE_PROTOCOL_MAX_WIRE_FRAME_SIZE);
    EXPECT_TRUE(native_protocol_decode_wire_frame(
                    wire,
                    wire_length,
                    &decoded) == NATIVE_PROTOCOL_DECODE_OK);
    EXPECT_TRUE(decoded.payload_length == source.payload_length);
    for (index = 0u; index < source.payload_length; ++index)
    {
        EXPECT_TRUE(decoded.payload[index] == source.payload[index]);
    }
}

static void test_command_service_rejects_invalid_identity_payload(void)
{
    static const uint8_t payload = 1u;
    const command_service_context_t context = {
        .product_id = COMMAND_SERVICE_PRODUCT_ID_MKS57D,
    };
    const command_request_t request = {
        .operation = COMMAND_OPERATION_GET_IDENTITY,
        .payload = &payload,
        .payload_length = 1u,
    };
    command_response_t response;

    command_service_dispatch(&context, &request, &response);
    EXPECT_TRUE(response.status == COMMAND_STATUS_INVALID_PAYLOAD);
    EXPECT_TRUE(response.kind == COMMAND_RESPONSE_NONE);
}

static void test_native_protocol_ping_round_trip_handles_zero_bytes(void)
{
    static const uint8_t payload[] = {0x11u, 0x00u, 0x22u};
    uint8_t wire[NATIVE_PROTOCOL_MAX_WIRE_FRAME_SIZE];
    native_protocol_server_t server;
    mock_protocol_tx_t transmit = {.accept = true};
    native_protocol_frame_t response;
    size_t wire_length;
    size_t index;

    EXPECT_TRUE(init_native_server(&server, &transmit));
    wire_length = encode_native_request(
        NATIVE_PROTOCOL_DEFAULT_DEVICE_ADDRESS,
        0x1234u,
        NATIVE_PROTOCOL_MESSAGE_REQUEST,
        NATIVE_PROTOCOL_COMMAND_PING,
        payload,
        sizeof(payload),
        wire,
        sizeof(wire));
    EXPECT_TRUE(wire_length != 0u);

    for (index = 0u; index < wire_length; ++index)
    {
        native_protocol_server_consume(&server, &wire[index], 1u);
    }

    EXPECT_TRUE(transmit.call_count == 1u);
    EXPECT_TRUE(native_protocol_decode_wire_frame(
                    transmit.bytes,
                    transmit.length,
                    &response) == NATIVE_PROTOCOL_DECODE_OK);
    EXPECT_TRUE(response.device_address ==
                NATIVE_PROTOCOL_DEFAULT_DEVICE_ADDRESS);
    EXPECT_TRUE(response.sequence == 0x1234u);
    EXPECT_TRUE(response.message_type == NATIVE_PROTOCOL_MESSAGE_RESPONSE);
    EXPECT_TRUE(response.command == NATIVE_PROTOCOL_COMMAND_PING);
    EXPECT_TRUE(response.payload_length == 4u);
    EXPECT_TRUE(response.payload[0] == NATIVE_PROTOCOL_STATUS_OK);
    EXPECT_TRUE(response.payload[1] == 0x11u);
    EXPECT_TRUE(response.payload[2] == 0x00u);
    EXPECT_TRUE(response.payload[3] == 0x22u);
}

static void test_native_protocol_reports_identity_and_capabilities(void)
{
    static const uint8_t identity_request[] = {
        0x03u, 0x01u, 0x01u, 0x03u, 0x01u, 0x01u,
        0x02u, 0x02u, 0x03u, 0x74u, 0x0Bu, 0x00u
    };
    uint8_t wire[NATIVE_PROTOCOL_MAX_WIRE_FRAME_SIZE];
    native_protocol_server_t server;
    mock_protocol_tx_t transmit = {.accept = true};
    native_protocol_frame_t response;
    size_t wire_length;
    size_t index;

    EXPECT_TRUE(init_native_server(&server, &transmit));
    wire_length = encode_native_request(
        NATIVE_PROTOCOL_DEFAULT_DEVICE_ADDRESS,
        1u,
        NATIVE_PROTOCOL_MESSAGE_REQUEST,
        NATIVE_PROTOCOL_COMMAND_GET_IDENTITY,
        NULL,
        0u,
        wire,
        sizeof(wire));
    EXPECT_TRUE(wire_length == sizeof(identity_request));
    for (index = 0u; index < sizeof(identity_request); ++index)
    {
        EXPECT_TRUE(wire[index] == identity_request[index]);
    }
    native_protocol_server_consume(&server, wire, wire_length);
    EXPECT_TRUE(native_protocol_decode_wire_frame(
                    transmit.bytes,
                    transmit.length,
                    &response) == NATIVE_PROTOCOL_DECODE_OK);
    EXPECT_TRUE(response.payload_length == 11u);
    EXPECT_TRUE(response.payload[0] == NATIVE_PROTOCOL_STATUS_OK);
    EXPECT_TRUE(response.payload[1] == 0x4Du);
    EXPECT_TRUE(response.payload[2] == 0x4Bu);
    EXPECT_TRUE(response.payload[3] == 0x53u);
    EXPECT_TRUE(response.payload[4] == 0x35u);
    EXPECT_TRUE(response.payload[5] == 0u);
    EXPECT_TRUE(response.payload[6] == 4u);
    EXPECT_TRUE(response.payload[7] == 0u);
    EXPECT_TRUE(response.payload[8] == 0u);
    EXPECT_TRUE(response.payload[9] == NATIVE_PROTOCOL_VERSION_MAJOR);
    EXPECT_TRUE(response.payload[10] == NATIVE_PROTOCOL_VERSION_MINOR);

    transmit.length = 0u;
    wire_length = encode_native_request(
        NATIVE_PROTOCOL_DEFAULT_DEVICE_ADDRESS,
        2u,
        NATIVE_PROTOCOL_MESSAGE_REQUEST,
        NATIVE_PROTOCOL_COMMAND_GET_CAPABILITIES,
        NULL,
        0u,
        wire,
        sizeof(wire));
    native_protocol_server_consume(&server, wire, wire_length);
    EXPECT_TRUE(native_protocol_decode_wire_frame(
                    transmit.bytes,
                    transmit.length,
                    &response) == NATIVE_PROTOCOL_DECODE_OK);
    EXPECT_TRUE(response.payload_length == 5u);
    EXPECT_TRUE(response.payload[0] == NATIVE_PROTOCOL_STATUS_OK);
    EXPECT_TRUE(response.payload[1] == 0u);
    EXPECT_TRUE(response.payload[2] == 0u);
    EXPECT_TRUE(response.payload[3] == 0xA5u);
    EXPECT_TRUE(response.payload[4] == 0x5Au);
}

static void test_native_protocol_commissioning_console_round_trip(void)
{
    static const uint8_t configure_payload[] = {
        0x00u, 0x2Au, 0x00u, 0x00u, 0x03u, 0xE8u
    };
    static const uint8_t start_payload[] = {
        0x02u, 0x00u, 0x00u, 0x13u, 0x88u
    };
    static const uint8_t trace_payload[] = {0x00u, 0x2Au};
    uint8_t wire[NATIVE_PROTOCOL_MAX_WIRE_FRAME_SIZE];
    native_protocol_server_t server;
    mock_protocol_tx_t transmit = {.accept = true};
    mock_commissioning_t commissioning = {
        .status = {
            .schema_version = 3u,
            .flags = 0x00000FFFu,
            .raw_input_levels = 0xA5u,
            .debounced_input_levels = 0x5Au,
            .adc_status = 3u,
            .selected_leg = 2u,
            .fault_flags = 0x00040000u,
            .sample_count = 0x01020304u,
            .current_a_raw = 0x0810u,
            .current_b_raw = 0x0820u,
            .current_a_zero_raw = 0x0800u,
            .current_b_zero_raw = 0x0801u,
            .current_a_reference_counts = -25,
            .current_b_reference_counts = 24,
            .current_a_measured_counts = -3,
            .current_b_measured_counts = 4,
            .phase_a_voltage_permille = -100,
            .phase_b_voltage_permille = 99,
            .duty_a1_permille = 450u,
            .duty_a2_permille = 550u,
            .duty_b1_permille = 549u,
            .duty_b2_permille = 451u,
            .test_amplitude_counts = 25u,
            .maximum_test_amplitude_counts = 50u,
            .hard_current_limit_counts = 100u,
            .phase_voltage_limit_permille = 100u,
            .test_frequency_millihz = 500u,
            .remote_run_remaining_millis = 4321u,
            .retained_panic = 15u,
            .watchdog_reset = 1u,
            .vbus_raw = 0x0708u,
            .vbus_sample_count = 0x11121314u,
        },
        .encoder_status = {
            .schema_version = 2u,
            .status = 1u,
            .transport_status = 0u,
            .angle_raw = 0x2345u,
            .flags = 0x02u,
            .sample_count = 0x01020304u,
            .error_count = 0x05060708u,
            .last_attempt_millis = 0x090A0B0Cu,
            .estimator_flags = 0x07u,
            .position_revolutions_q16_16 = 0x11223344,
            .velocity_revolutions_per_second_q16_16 = -2,
            .estimator_timestamp_us = 0x0D0E0F10u,
            .estimator_fault_flags = 0x11121314u,
            .alignment_zero_raw = 0x3456u,
            .alignment_direction = -1,
            .electrical_phase_q32 = 0x89ABCDEFu,
            .estimator_sample_interval_us = 0x21222324u,
            .estimator_maximum_sample_interval_us = 0x25262728u,
        },
        .current_trace = {
            .schema_version = 1u,
            .captured_sample_count = 256u,
            .sample_index = 42u,
            .loop_sample_count = 0x01020304u,
            .current_a_reference_counts = -50,
            .current_b_reference_counts = 49,
            .current_a_measured_counts = -48,
            .current_b_measured_counts = 47,
            .phase_a_voltage_permille = -100,
            .phase_b_voltage_permille = 99,
        },
        .alignment_status = {
            .schema_version = 1u,
            .state = 7u,
            .result = 1u,
            .flags = 0x0Fu,
            .alignment_current_counts = 125u,
            .phase_zero_raw = 14249u,
            .phase_quarter_raw = 14165u,
            .return_zero_raw = 14250u,
            .observed_quarter_step_counts = 84u,
            .quarter_step_error_counts = 2,
            .closure_error_counts = 1,
            .encoder_direction = -1,
            .active_sample_count = 101u,
            .elapsed_millis = 2550u,
            .remaining_millis = 1450u,
            .minimum_current_counts = 50u,
            .maximum_current_counts = 165u,
            .expected_quarter_step_counts = 82u,
            .maximum_quarter_step_error_counts = 12u,
            .settle_duration_millis = 750u,
            .sample_duration_millis = 100u,
            .maximum_duration_millis = 4000u,
            .minimum_sample_count = 64u,
            .maximum_sample_span_counts = 8u,
            .maximum_closure_error_counts = 12u,
            .maximum_current_error_counts = 8u,
        },
        .configuration_status = {
            .schema_version = 1u,
            .flags = 0xFFu,
            .last_result = 0u,
            .active_slot = 1u,
            .record_schema_version = 1u,
            .generation = 0x01020304u,
            .stored_encoder_counts_per_revolution = 16384u,
            .stored_electrical_cycles_per_revolution = 50u,
            .stored_electrical_zero_raw = 9302u,
            .stored_observed_quarter_step_counts = 80u,
            .stored_quarter_step_error_counts = -2,
            .stored_encoder_direction = -1,
            .active_encoder_counts_per_revolution = 16384u,
            .active_electrical_cycles_per_revolution = 50u,
            .active_electrical_zero_raw = 9302u,
            .active_observed_quarter_step_counts = 80u,
            .active_quarter_step_error_counts = -2,
            .active_encoder_direction = -1,
        },
        .aligned_torque_status = {
            .schema_version = 2u,
            .state = 2u,
            .result = 0u,
            .flags = 0x3Fu,
            .fault_flags = 0x01020304u,
            .requested_q_current_counts = -50,
            .applied_q_current_counts = -49,
            .current_a_reference_counts = 48,
            .current_b_reference_counts = -47,
            .electrical_phase_q32 = 0x11223344u,
            .velocity_revolutions_per_second_q16_16 = -65536,
            .acceleration_revolutions_per_second2_q16_16 = 0x00140000,
            .elapsed_millis = 0x05060708u,
            .remaining_millis = 0x090A0B0Cu,
            .maximum_current_counts = 495u,
            .maximum_current_slew_counts_per_second = 10000u,
            .maximum_velocity_revolutions_per_second_q16_16 = 5 * 65536,
            .maximum_acceleration_revolutions_per_second2_q16_16 =
                1000 * 65536,
            .maximum_feedback_interval_us = 2000u,
            .minimum_duration_millis = 3u,
            .maximum_duration_millis = INT32_MAX,
            .backend_fault_flags = 0xA1B2C3D4u,
            .phase_prediction_reject_reason =
                CURRENT_LOOP_PHASE_PREDICTION_REJECT_STALE,
            .rejected_phase_prediction_age_us = 0x01020304u,
            .maximum_observed_phase_prediction_age_us = 0x0ABCu,
            .maximum_phase_prediction_age_us = 3000u,
        },
        .velocity_status = {
            .schema_version = 1u,
            .state = 2u,
            .result = 0u,
            .flags = 0x7Fu,
            .fault_flags = 0x01020304u,
            .target_velocity_revolutions_per_second_q16_16 = 0x00008000,
            .reference_velocity_revolutions_per_second_q16_16 = 0x00004000,
            .measured_velocity_revolutions_per_second_q16_16 = -0x00002000,
            .requested_q_current_counts = -25,
            .applied_q_current_counts = -24,
            .current_limit_counts = 100u,
            .elapsed_millis = 0x01020304u,
            .remaining_millis = 0x05060708u,
            .maximum_target_velocity_revolutions_per_second_q16_16 =
                1 * 65536,
            .maximum_target_acceleration_revolutions_per_second2_q16_16 =
                1 * 65536,
            .maximum_feedback_velocity_revolutions_per_second_q16_16 =
                5 * 65536,
            .maximum_current_counts = 100u,
            .maximum_feedback_interval_us = 2000u,
            .proportional_gain_current_counts_per_velocity_q16_16 =
                100 * 65536,
            .integral_gain_current_counts_per_position_q16_16 =
                200 * 65536,
            .maximum_duration_millis = INT32_MAX,
        },
        .position_status = {
            .schema_version = 1u,
            .state = 2u,
            .result = 0u,
            .flags = 0xFFu,
            .fault_flags = 0x01020304u,
            .target_position_revolutions_q16_16 = 0x00018000,
            .reference_position_revolutions_q16_16 = 0x00014000,
            .measured_position_revolutions_q16_16 = -0x00008000,
            .reference_velocity_revolutions_per_second_q16_16 =
                0x00004000,
            .target_velocity_revolutions_per_second_q16_16 =
                -0x00004000,
            .measured_velocity_revolutions_per_second_q16_16 =
                0x00002000,
            .requested_q_current_counts = -25,
            .applied_q_current_counts = -24,
            .current_limit_counts = 100u,
            .elapsed_millis = 0x01020304u,
            .remaining_millis = 0x05060708u,
            .maximum_relative_travel_revolutions_q16_16 = 100 * 65536,
            .maximum_velocity_revolutions_per_second_q16_16 = 4 * 65536,
            .maximum_acceleration_revolutions_per_second2_q16_16 =
                4 * 65536,
            .maximum_following_error_revolutions_q16_16 = 1 * 65536 / 4,
        },
        .fault_recovery_status = {
            .schema_version = 1u,
            .result = COMMAND_FAULT_RECOVERY_RESULT_BLOCKED,
            .blocker_flags =
                COMMAND_FAULT_RECOVERY_BLOCKER_BACKEND_RESET_FAILED,
            .cleared_fault_flags =
                COMMAND_FAULT_SOURCE_SUPERVISOR |
                COMMAND_FAULT_SOURCE_VELOCITY |
                COMMAND_FAULT_SOURCE_POSITION,
            .remaining_fault_flags =
                COMMAND_FAULT_SOURCE_CURRENT_BACKEND,
        },
    };
    native_protocol_frame_t response;
    size_t wire_length;

    EXPECT_TRUE(init_commissioning_server(
        &server,
        &transmit,
        &commissioning));
    wire_length = encode_native_request(
        NATIVE_PROTOCOL_DEFAULT_DEVICE_ADDRESS,
        20u,
        NATIVE_PROTOCOL_MESSAGE_REQUEST,
        NATIVE_PROTOCOL_COMMAND_GET_COMMISSIONING_STATUS,
        NULL,
        0u,
        wire,
        sizeof(wire));
    native_protocol_server_consume(&server, wire, wire_length);
    EXPECT_TRUE(native_protocol_decode_wire_frame(
                    transmit.bytes,
                    transmit.length,
                    &response) == NATIVE_PROTOCOL_DECODE_OK);
    EXPECT_TRUE(commissioning.status_calls == 1u);
    EXPECT_TRUE(response.payload_length == 70u);
    EXPECT_TRUE(response.payload[0] == NATIVE_PROTOCOL_STATUS_OK);
    EXPECT_TRUE(response.payload[1] == 3u);
    EXPECT_TRUE(response.payload[2] == 0u);
    EXPECT_TRUE(response.payload[5] == 0xFFu);
    EXPECT_TRUE(response.payload[6] == 0xA5u);
    EXPECT_TRUE(response.payload[7] == 0x5Au);
    EXPECT_TRUE(response.payload[26] == 0xFFu);
    EXPECT_TRUE(response.payload[27] == 0xE7u);
    EXPECT_TRUE(response.payload[54] == 0u);
    EXPECT_TRUE(response.payload[57] == 0xF4u);
    EXPECT_TRUE(response.payload[60] == 0x10u);
    EXPECT_TRUE(response.payload[61] == 0xE1u);
    EXPECT_TRUE(response.payload[62] == 15u);
    EXPECT_TRUE(response.payload[63] == 1u);
    EXPECT_TRUE(response.payload[64] == 0x07u);
    EXPECT_TRUE(response.payload[65] == 0x08u);
    EXPECT_TRUE(response.payload[66] == 0x11u);
    EXPECT_TRUE(response.payload[69] == 0x14u);

    wire_length = encode_native_request(
        NATIVE_PROTOCOL_DEFAULT_DEVICE_ADDRESS,
        21u,
        NATIVE_PROTOCOL_MESSAGE_REQUEST,
        NATIVE_PROTOCOL_COMMAND_CONFIGURE_CURRENT_TEST,
        configure_payload,
        sizeof(configure_payload),
        wire,
        sizeof(wire));
    native_protocol_server_consume(&server, wire, wire_length);
    EXPECT_TRUE(native_protocol_decode_wire_frame(
                    transmit.bytes,
                    transmit.length,
                    &response) == NATIVE_PROTOCOL_DECODE_OK);
    EXPECT_TRUE(commissioning.configure_calls == 1u);
    EXPECT_TRUE(commissioning.requested_config.amplitude_counts == 42u);
    EXPECT_TRUE(commissioning.requested_config.frequency_millihz == 1000u);
    EXPECT_TRUE(response.payload_length == 7u);
    EXPECT_TRUE(response.payload[1] == 0u);
    EXPECT_TRUE(response.payload[2] == 42u);

    wire_length = encode_native_request(
        NATIVE_PROTOCOL_DEFAULT_DEVICE_ADDRESS,
        22u,
        NATIVE_PROTOCOL_MESSAGE_REQUEST,
        NATIVE_PROTOCOL_COMMAND_START_CURRENT_TEST,
        start_payload,
        sizeof(start_payload),
        wire,
        sizeof(wire));
    native_protocol_server_consume(&server, wire, wire_length);
    EXPECT_TRUE(native_protocol_decode_wire_frame(
                    transmit.bytes,
                    transmit.length,
                    &response) == NATIVE_PROTOCOL_DECODE_OK);
    EXPECT_TRUE(commissioning.start_calls == 1u);
    EXPECT_TRUE(commissioning.requested_leg == 2u);
    EXPECT_TRUE(commissioning.requested_duration_millis == 5000u);
    EXPECT_TRUE(response.payload_length == 1u);
    EXPECT_TRUE(response.payload[0] == NATIVE_PROTOCOL_STATUS_OK);

    wire_length = encode_native_request(
        NATIVE_PROTOCOL_DEFAULT_DEVICE_ADDRESS,
        99u,
        NATIVE_PROTOCOL_MESSAGE_REQUEST,
        NATIVE_PROTOCOL_COMMAND_CLEAR_FAULTS,
        NULL,
        0u,
        wire,
        sizeof(wire));
    native_protocol_server_consume(&server, wire, wire_length);
    EXPECT_TRUE(native_protocol_decode_wire_frame(
                    transmit.bytes,
                    transmit.length,
                    &response) == NATIVE_PROTOCOL_DECODE_OK);
    EXPECT_TRUE(commissioning.drive_clear_faults_calls == 1u);
    EXPECT_TRUE(response.payload_length == 15u);
    EXPECT_TRUE(response.payload[0] == NATIVE_PROTOCOL_STATUS_OK);
    EXPECT_TRUE(response.payload[1] == 1u);
    EXPECT_TRUE(response.payload[2] ==
                COMMAND_FAULT_RECOVERY_RESULT_BLOCKED);
    EXPECT_TRUE(response.payload[6] == 0x02u);
    EXPECT_TRUE(response.payload[10] == 0x31u);
    EXPECT_TRUE(response.payload[14] == 0x40u);

    wire_length = encode_native_request(
        NATIVE_PROTOCOL_DEFAULT_DEVICE_ADDRESS,
        23u,
        NATIVE_PROTOCOL_MESSAGE_REQUEST,
        NATIVE_PROTOCOL_COMMAND_STOP_CURRENT_TEST,
        NULL,
        0u,
        wire,
        sizeof(wire));
    native_protocol_server_consume(&server, wire, wire_length);
    EXPECT_TRUE(native_protocol_decode_wire_frame(
                    transmit.bytes,
                    transmit.length,
                    &response) == NATIVE_PROTOCOL_DECODE_OK);
    EXPECT_TRUE(commissioning.stop_calls == 1u);
    EXPECT_TRUE(response.payload_length == 1u);
    EXPECT_TRUE(response.payload[0] == NATIVE_PROTOCOL_STATUS_OK);

    wire_length = encode_native_request(
        NATIVE_PROTOCOL_DEFAULT_DEVICE_ADDRESS,
        24u,
        NATIVE_PROTOCOL_MESSAGE_REQUEST,
        NATIVE_PROTOCOL_COMMAND_GET_BOOT_STATUS,
        NULL,
        0u,
        wire,
        sizeof(wire));
    native_protocol_server_consume(&server, wire, wire_length);
    EXPECT_TRUE(native_protocol_decode_wire_frame(
                    transmit.bytes,
                    transmit.length,
                    &response) == NATIVE_PROTOCOL_DECODE_OK);
    EXPECT_TRUE(commissioning.boot_status_calls == 1u);
    EXPECT_TRUE(response.payload_length == 11u);
    EXPECT_TRUE(response.payload[0] == NATIVE_PROTOCOL_STATUS_OK);
    EXPECT_TRUE(response.payload[1] == 1u);
    EXPECT_TRUE(response.payload[2] == 0x28u);
    EXPECT_TRUE(response.payload[6] == 15u);
    EXPECT_TRUE(response.payload[7] == 1u);
    EXPECT_TRUE(response.payload[10] == 4u);

    wire_length = encode_native_request(
        NATIVE_PROTOCOL_DEFAULT_DEVICE_ADDRESS,
        25u,
        NATIVE_PROTOCOL_MESSAGE_REQUEST,
        NATIVE_PROTOCOL_COMMAND_GET_ENCODER_STATUS,
        NULL,
        0u,
        wire,
        sizeof(wire));
    native_protocol_server_consume(&server, wire, wire_length);
    EXPECT_TRUE(native_protocol_decode_wire_frame(
                    transmit.bytes,
                    transmit.length,
                    &response) == NATIVE_PROTOCOL_DECODE_OK);
    EXPECT_TRUE(commissioning.encoder_status_calls == 1u);
    EXPECT_TRUE(response.payload_length == 51u);
    EXPECT_TRUE(response.payload[0] == NATIVE_PROTOCOL_STATUS_OK);
    EXPECT_TRUE(response.payload[1] == 2u);
    EXPECT_TRUE(response.payload[2] == 1u);
    EXPECT_TRUE(response.payload[3] == 0u);
    EXPECT_TRUE(response.payload[4] == 0x23u);
    EXPECT_TRUE(response.payload[5] == 0x45u);
    EXPECT_TRUE(response.payload[6] == 0x02u);
    EXPECT_TRUE(response.payload[7] == 0x01u);
    EXPECT_TRUE(response.payload[10] == 0x04u);
    EXPECT_TRUE(response.payload[11] == 0x05u);
    EXPECT_TRUE(response.payload[14] == 0x08u);
    EXPECT_TRUE(response.payload[15] == 0x09u);
    EXPECT_TRUE(response.payload[18] == 0x0Cu);
    EXPECT_TRUE(response.payload[19] == 0x07u);
    EXPECT_TRUE(response.payload[20] == 0x11u);
    EXPECT_TRUE(response.payload[23] == 0x44u);
    EXPECT_TRUE(response.payload[24] == 0xFFu);
    EXPECT_TRUE(response.payload[27] == 0xFEu);
    EXPECT_TRUE(response.payload[28] == 0x0Du);
    EXPECT_TRUE(response.payload[31] == 0x10u);
    EXPECT_TRUE(response.payload[32] == 0x11u);
    EXPECT_TRUE(response.payload[35] == 0x14u);
    EXPECT_TRUE(response.payload[36] == 0x34u);
    EXPECT_TRUE(response.payload[37] == 0x56u);
    EXPECT_TRUE(response.payload[38] == 0xFFu);
    EXPECT_TRUE(response.payload[39] == 0x89u);
    EXPECT_TRUE(response.payload[42] == 0xEFu);
    EXPECT_TRUE(response.payload[43] == 0x21u);
    EXPECT_TRUE(response.payload[46] == 0x24u);
    EXPECT_TRUE(response.payload[47] == 0x25u);
    EXPECT_TRUE(response.payload[50] == 0x28u);

    wire_length = encode_native_request(
        NATIVE_PROTOCOL_DEFAULT_DEVICE_ADDRESS,
        26u,
        NATIVE_PROTOCOL_MESSAGE_REQUEST,
        NATIVE_PROTOCOL_COMMAND_GET_CURRENT_TRACE,
        trace_payload,
        sizeof(trace_payload),
        wire,
        sizeof(wire));
    native_protocol_server_consume(&server, wire, wire_length);
    EXPECT_TRUE(native_protocol_decode_wire_frame(
                    transmit.bytes,
                    transmit.length,
                    &response) == NATIVE_PROTOCOL_DECODE_OK);
    EXPECT_TRUE(commissioning.current_trace_calls == 1u);
    EXPECT_TRUE(commissioning.requested_trace_index == 42u);
    EXPECT_TRUE(response.payload_length == 22u);
    EXPECT_TRUE(response.payload[0] == NATIVE_PROTOCOL_STATUS_OK);
    EXPECT_TRUE(response.payload[1] == 1u);
    EXPECT_TRUE(response.payload[2] == 0x01u);
    EXPECT_TRUE(response.payload[3] == 0x00u);
    EXPECT_TRUE(response.payload[4] == 0u);
    EXPECT_TRUE(response.payload[5] == 42u);
    EXPECT_TRUE(response.payload[6] == 0x01u);
    EXPECT_TRUE(response.payload[9] == 0x04u);
    EXPECT_TRUE(response.payload[10] == 0xFFu);
    EXPECT_TRUE(response.payload[11] == 0xCEu);
    EXPECT_TRUE(response.payload[20] == 0u);
    EXPECT_TRUE(response.payload[21] == 99u);

    {
        static const uint8_t alignment_payload[] = {0x00u, 0x7Du};

        wire_length = encode_native_request(
            NATIVE_PROTOCOL_DEFAULT_DEVICE_ADDRESS,
            27u,
            NATIVE_PROTOCOL_MESSAGE_REQUEST,
            NATIVE_PROTOCOL_COMMAND_START_ALIGNMENT,
            alignment_payload,
            sizeof(alignment_payload),
            wire,
            sizeof(wire));
        native_protocol_server_consume(&server, wire, wire_length);
        EXPECT_TRUE(native_protocol_decode_wire_frame(
                        transmit.bytes,
                        transmit.length,
                        &response) == NATIVE_PROTOCOL_DECODE_OK);
        EXPECT_TRUE(commissioning.alignment_start_calls == 1u);
        EXPECT_TRUE(commissioning.requested_alignment_current_counts ==
                    125u);
        EXPECT_TRUE(response.payload_length == 1u);
        EXPECT_TRUE(response.payload[0] == NATIVE_PROTOCOL_STATUS_OK);
    }

    wire_length = encode_native_request(
        NATIVE_PROTOCOL_DEFAULT_DEVICE_ADDRESS,
        28u,
        NATIVE_PROTOCOL_MESSAGE_REQUEST,
        NATIVE_PROTOCOL_COMMAND_GET_ALIGNMENT_STATUS,
        NULL,
        0u,
        wire,
        sizeof(wire));
    native_protocol_server_consume(&server, wire, wire_length);
    EXPECT_TRUE(native_protocol_decode_wire_frame(
                    transmit.bytes,
                    transmit.length,
                    &response) == NATIVE_PROTOCOL_DECODE_OK);
    EXPECT_TRUE(commissioning.alignment_status_calls == 1u);
    EXPECT_TRUE(response.payload_length == 58u);
    EXPECT_TRUE(response.payload[0] == NATIVE_PROTOCOL_STATUS_OK);
    EXPECT_TRUE(response.payload[1] == 1u);
    EXPECT_TRUE(response.payload[2] == 7u);
    EXPECT_TRUE(response.payload[3] == 1u);
    EXPECT_TRUE(response.payload[4] == 0x0Fu);
    EXPECT_TRUE(response.payload[5] == 0u);
    EXPECT_TRUE(response.payload[6] == 125u);
    EXPECT_TRUE(response.payload[7] == 0x37u);
    EXPECT_TRUE(response.payload[8] == 0xA9u);
    EXPECT_TRUE(response.payload[9] == 0x37u);
    EXPECT_TRUE(response.payload[10] == 0x55u);
    EXPECT_TRUE(response.payload[11] == 0x37u);
    EXPECT_TRUE(response.payload[12] == 0xAAu);
    EXPECT_TRUE(response.payload[13] == 0u);
    EXPECT_TRUE(response.payload[14] == 84u);
    EXPECT_TRUE(response.payload[15] == 0u);
    EXPECT_TRUE(response.payload[16] == 2u);
    EXPECT_TRUE(response.payload[17] == 0u);
    EXPECT_TRUE(response.payload[18] == 1u);
    EXPECT_TRUE(response.payload[19] == 0xFFu);
    EXPECT_TRUE(response.payload[20] == 0u);
    EXPECT_TRUE(response.payload[21] == 101u);
    EXPECT_TRUE(response.payload[22] == 0u);
    EXPECT_TRUE(response.payload[25] == 0xF6u);
    EXPECT_TRUE(response.payload[26] == 0u);
    EXPECT_TRUE(response.payload[29] == 0xAAu);
    EXPECT_TRUE(response.payload[30] == 0u);
    EXPECT_TRUE(response.payload[31] == 50u);
    EXPECT_TRUE(response.payload[32] == 0u);
    EXPECT_TRUE(response.payload[33] == 165u);
    EXPECT_TRUE(response.payload[34] == 0u);
    EXPECT_TRUE(response.payload[35] == 82u);
    EXPECT_TRUE(response.payload[36] == 0u);
    EXPECT_TRUE(response.payload[37] == 12u);
    EXPECT_TRUE(response.payload[38] == 0u);
    EXPECT_TRUE(response.payload[41] == 0xEEu);
    EXPECT_TRUE(response.payload[42] == 0u);
    EXPECT_TRUE(response.payload[45] == 100u);
    EXPECT_TRUE(response.payload[46] == 0u);
    EXPECT_TRUE(response.payload[49] == 0xA0u);
    EXPECT_TRUE(response.payload[50] == 0u);
    EXPECT_TRUE(response.payload[51] == 64u);
    EXPECT_TRUE(response.payload[52] == 0u);
    EXPECT_TRUE(response.payload[53] == 8u);
    EXPECT_TRUE(response.payload[54] == 0u);
    EXPECT_TRUE(response.payload[55] == 12u);
    EXPECT_TRUE(response.payload[56] == 0u);
    EXPECT_TRUE(response.payload[57] == 8u);

    wire_length = encode_native_request(
        NATIVE_PROTOCOL_DEFAULT_DEVICE_ADDRESS,
        29u,
        NATIVE_PROTOCOL_MESSAGE_REQUEST,
        NATIVE_PROTOCOL_COMMAND_STOP_DRIVE,
        NULL,
        0u,
        wire,
        sizeof(wire));
    native_protocol_server_consume(&server, wire, wire_length);
    EXPECT_TRUE(native_protocol_decode_wire_frame(
                    transmit.bytes,
                    transmit.length,
                    &response) == NATIVE_PROTOCOL_DECODE_OK);
    EXPECT_TRUE(commissioning.drive_stop_calls == 1u);
    EXPECT_TRUE(response.payload_length == 1u);
    EXPECT_TRUE(response.payload[0] == NATIVE_PROTOCOL_STATUS_OK);

    wire_length = encode_native_request(
        NATIVE_PROTOCOL_DEFAULT_DEVICE_ADDRESS,
        30u,
        NATIVE_PROTOCOL_MESSAGE_REQUEST,
        NATIVE_PROTOCOL_COMMAND_GET_CONFIGURATION_STATUS,
        NULL,
        0u,
        wire,
        sizeof(wire));
    native_protocol_server_consume(&server, wire, wire_length);
    EXPECT_TRUE(native_protocol_decode_wire_frame(
                    transmit.bytes,
                    transmit.length,
                    &response) == NATIVE_PROTOCOL_DECODE_OK);
    EXPECT_TRUE(commissioning.configuration_status_calls == 1u);
    EXPECT_TRUE(response.payload_length == 33u);
    EXPECT_TRUE(response.payload[0] == NATIVE_PROTOCOL_STATUS_OK);
    EXPECT_TRUE(response.payload[1] == 1u);
    EXPECT_TRUE(response.payload[2] == 0xFFu);
    EXPECT_TRUE(response.payload[4] == 1u);
    EXPECT_TRUE(response.payload[5] == 0u);
    EXPECT_TRUE(response.payload[6] == 1u);
    EXPECT_TRUE(response.payload[7] == 1u);
    EXPECT_TRUE(response.payload[10] == 4u);
    EXPECT_TRUE(response.payload[11] == 0x40u);
    EXPECT_TRUE(response.payload[12] == 0u);
    EXPECT_TRUE(response.payload[13] == 0u);
    EXPECT_TRUE(response.payload[14] == 50u);
    EXPECT_TRUE(response.payload[15] == 0x24u);
    EXPECT_TRUE(response.payload[16] == 0x56u);
    EXPECT_TRUE(response.payload[17] == 0u);
    EXPECT_TRUE(response.payload[18] == 80u);
    EXPECT_TRUE(response.payload[19] == 0xFFu);
    EXPECT_TRUE(response.payload[20] == 0xFEu);
    EXPECT_TRUE(response.payload[21] == 0xFFu);
    EXPECT_TRUE(response.payload[22] == 0x40u);
    EXPECT_TRUE(response.payload[32] == 0xFFu);

    wire_length = encode_native_request(
        NATIVE_PROTOCOL_DEFAULT_DEVICE_ADDRESS,
        31u,
        NATIVE_PROTOCOL_MESSAGE_REQUEST,
        NATIVE_PROTOCOL_COMMAND_SAVE_CONFIGURATION,
        NULL,
        0u,
        wire,
        sizeof(wire));
    native_protocol_server_consume(&server, wire, wire_length);
    EXPECT_TRUE(native_protocol_decode_wire_frame(
                    transmit.bytes,
                    transmit.length,
                    &response) == NATIVE_PROTOCOL_DECODE_OK);
    EXPECT_TRUE(commissioning.configuration_save_calls == 1u);
    EXPECT_TRUE(response.payload_length == 1u);
    EXPECT_TRUE(response.payload[0] == NATIVE_PROTOCOL_STATUS_OK);

    wire_length = encode_native_request(
        NATIVE_PROTOCOL_DEFAULT_DEVICE_ADDRESS,
        32u,
        NATIVE_PROTOCOL_MESSAGE_REQUEST,
        NATIVE_PROTOCOL_COMMAND_CLEAR_CALIBRATION,
        NULL,
        0u,
        wire,
        sizeof(wire));
    native_protocol_server_consume(&server, wire, wire_length);
    EXPECT_TRUE(native_protocol_decode_wire_frame(
                    transmit.bytes,
                    transmit.length,
                    &response) == NATIVE_PROTOCOL_DECODE_OK);
    EXPECT_TRUE(commissioning.calibration_clear_calls == 1u);
    EXPECT_TRUE(response.payload_length == 1u);
    EXPECT_TRUE(response.payload[0] == NATIVE_PROTOCOL_STATUS_OK);

    {
        static const uint8_t torque_payload[] = {
            0xFFu, 0xCEu, 0x00u, 0x00u, 0x13u, 0x88u};

        wire_length = encode_native_request(
            NATIVE_PROTOCOL_DEFAULT_DEVICE_ADDRESS,
            33u,
            NATIVE_PROTOCOL_MESSAGE_REQUEST,
            NATIVE_PROTOCOL_COMMAND_START_ALIGNED_TORQUE,
            torque_payload,
            sizeof(torque_payload),
            wire,
            sizeof(wire));
        native_protocol_server_consume(&server, wire, wire_length);
        EXPECT_TRUE(native_protocol_decode_wire_frame(
                        transmit.bytes,
                        transmit.length,
                        &response) == NATIVE_PROTOCOL_DECODE_OK);
        EXPECT_TRUE(commissioning.aligned_torque_start_calls == 1u);
        EXPECT_TRUE(commissioning.requested_q_current_counts == -50);
        EXPECT_TRUE(commissioning.requested_torque_duration_millis == 5000u);
        EXPECT_TRUE(response.payload_length == 1u);
        EXPECT_TRUE(response.payload[0] == NATIVE_PROTOCOL_STATUS_OK);
    }

    wire_length = encode_native_request(
        NATIVE_PROTOCOL_DEFAULT_DEVICE_ADDRESS,
        34u,
        NATIVE_PROTOCOL_MESSAGE_REQUEST,
        NATIVE_PROTOCOL_COMMAND_GET_ALIGNED_TORQUE_STATUS,
        NULL,
        0u,
        wire,
        sizeof(wire));
    native_protocol_server_consume(&server, wire, wire_length);
    EXPECT_TRUE(native_protocol_decode_wire_frame(
                    transmit.bytes,
                    transmit.length,
                    &response) == NATIVE_PROTOCOL_DECODE_OK);
    EXPECT_TRUE(commissioning.aligned_torque_status_calls == 1u);
    EXPECT_TRUE(response.payload_length == 72u);
    EXPECT_TRUE(response.payload[0] == NATIVE_PROTOCOL_STATUS_OK);
    EXPECT_TRUE(response.payload[1] == 2u);
    EXPECT_TRUE(response.payload[2] == 2u);
    EXPECT_TRUE(response.payload[4] == 0x3Fu);
    EXPECT_TRUE(response.payload[5] == 0x01u);
    EXPECT_TRUE(response.payload[8] == 0x04u);
    EXPECT_TRUE(response.payload[9] == 0xFFu);
    EXPECT_TRUE(response.payload[10] == 0xCEu);
    EXPECT_TRUE(response.payload[11] == 0xFFu);
    EXPECT_TRUE(response.payload[12] == 0xCFu);
    EXPECT_TRUE(response.payload[13] == 0u);
    EXPECT_TRUE(response.payload[14] == 48u);
    EXPECT_TRUE(response.payload[15] == 0xFFu);
    EXPECT_TRUE(response.payload[16] == 0xD1u);
    EXPECT_TRUE(response.payload[17] == 0x11u);
    EXPECT_TRUE(response.payload[20] == 0x44u);
    EXPECT_TRUE(response.payload[21] == 0xFFu);
    EXPECT_TRUE(response.payload[24] == 0u);
    EXPECT_TRUE(response.payload[25] == 0u);
    EXPECT_TRUE(response.payload[26] == 0x14u);
    EXPECT_TRUE(response.payload[29] == 0x05u);
    EXPECT_TRUE(response.payload[36] == 0x0Cu);
    EXPECT_TRUE(response.payload[37] == 0x01u);
    EXPECT_TRUE(response.payload[38] == 0xEFu);
    EXPECT_TRUE(response.payload[39] == 0x27u);
    EXPECT_TRUE(response.payload[40] == 0x10u);
    EXPECT_TRUE(response.payload[41] == 0u);
    EXPECT_TRUE(response.payload[42] == 5u);
    EXPECT_TRUE(response.payload[43] == 0u);
    EXPECT_TRUE(response.payload[44] == 0u);
    EXPECT_TRUE(response.payload[45] == 0x03u);
    EXPECT_TRUE(response.payload[46] == 0xE8u);
    EXPECT_TRUE(response.payload[47] == 0u);
    EXPECT_TRUE(response.payload[48] == 0u);
    EXPECT_TRUE(response.payload[49] == 0x07u);
    EXPECT_TRUE(response.payload[50] == 0xD0u);
    EXPECT_TRUE(response.payload[54] == 3u);
    EXPECT_TRUE(response.payload[55] == 0x7Fu);
    EXPECT_TRUE(response.payload[56] == 0xFFu);
    EXPECT_TRUE(response.payload[57] == 0xFFu);
    EXPECT_TRUE(response.payload[58] == 0xFFu);
    EXPECT_TRUE(response.payload[59] == 0xA1u);
    EXPECT_TRUE(response.payload[62] == 0xD4u);
    EXPECT_TRUE(response.payload[63] ==
                CURRENT_LOOP_PHASE_PREDICTION_REJECT_STALE);
    EXPECT_TRUE(response.payload[64] == 0x01u);
    EXPECT_TRUE(response.payload[67] == 0x04u);
    EXPECT_TRUE(response.payload[68] == 0x0Au);
    EXPECT_TRUE(response.payload[69] == 0xBCu);
    EXPECT_TRUE(response.payload[70] == 0x0Bu);
    EXPECT_TRUE(response.payload[71] == 0xB8u);

    {
        static const uint8_t velocity_payload[] = {
            0xFFu, 0xFFu, 0x80u, 0x00u,
            0x00u, 0x64u,
            0x00u, 0x00u, 0x13u, 0x88u};

        wire_length = encode_native_request(
            NATIVE_PROTOCOL_DEFAULT_DEVICE_ADDRESS,
            35u,
            NATIVE_PROTOCOL_MESSAGE_REQUEST,
            NATIVE_PROTOCOL_COMMAND_START_VELOCITY,
            velocity_payload,
            sizeof(velocity_payload),
            wire,
            sizeof(wire));
        native_protocol_server_consume(&server, wire, wire_length);
        EXPECT_TRUE(native_protocol_decode_wire_frame(
                        transmit.bytes,
                        transmit.length,
                        &response) == NATIVE_PROTOCOL_DECODE_OK);
        EXPECT_TRUE(commissioning.velocity_start_calls == 1u);
        EXPECT_TRUE(
            commissioning.
                requested_velocity_revolutions_per_second_q16_16 ==
            -32768);
        EXPECT_TRUE(
            commissioning.requested_velocity_current_limit_counts == 100u);
        EXPECT_TRUE(
            commissioning.requested_velocity_duration_millis == 5000u);
        EXPECT_TRUE(response.payload_length == 1u);
        EXPECT_TRUE(response.payload[0] == NATIVE_PROTOCOL_STATUS_OK);
    }

    wire_length = encode_native_request(
        NATIVE_PROTOCOL_DEFAULT_DEVICE_ADDRESS,
        36u,
        NATIVE_PROTOCOL_MESSAGE_REQUEST,
        NATIVE_PROTOCOL_COMMAND_GET_VELOCITY_STATUS,
        NULL,
        0u,
        wire,
        sizeof(wire));
    native_protocol_server_consume(&server, wire, wire_length);
    EXPECT_TRUE(native_protocol_decode_wire_frame(
                    transmit.bytes,
                    transmit.length,
                    &response) == NATIVE_PROTOCOL_DECODE_OK);
    EXPECT_TRUE(commissioning.velocity_status_calls == 1u);
    EXPECT_TRUE(response.payload_length == 63u);
    EXPECT_TRUE(response.payload[0] == NATIVE_PROTOCOL_STATUS_OK);
    EXPECT_TRUE(response.payload[1] == 1u);
    EXPECT_TRUE(response.payload[2] == 2u);
    EXPECT_TRUE(response.payload[4] == 0x7Fu);
    EXPECT_TRUE(response.payload[5] == 0x01u);
    EXPECT_TRUE(response.payload[8] == 0x04u);
    EXPECT_TRUE(response.payload[9] == 0u);
    EXPECT_TRUE(response.payload[10] == 0u);
    EXPECT_TRUE(response.payload[11] == 0x80u);
    EXPECT_TRUE(response.payload[12] == 0u);
    EXPECT_TRUE(response.payload[17] == 0xFFu);
    EXPECT_TRUE(response.payload[20] == 0u);
    EXPECT_TRUE(response.payload[21] == 0xFFu);
    EXPECT_TRUE(response.payload[22] == 0xE7u);
    EXPECT_TRUE(response.payload[25] == 0u);
    EXPECT_TRUE(response.payload[26] == 100u);
    EXPECT_TRUE(response.payload[27] == 0x01u);
    EXPECT_TRUE(response.payload[34] == 0x08u);
    EXPECT_TRUE(response.payload[36] == 0x01u);
    EXPECT_TRUE(response.payload[44] == 0x05u);
    EXPECT_TRUE(response.payload[47] == 0u);
    EXPECT_TRUE(response.payload[48] == 100u);
    EXPECT_TRUE(response.payload[49] == 0x07u);
    EXPECT_TRUE(response.payload[50] == 0xD0u);
    EXPECT_TRUE(response.payload[52] == 0x64u);
    EXPECT_TRUE(response.payload[56] == 0xC8u);
    EXPECT_TRUE(response.payload[59] == 0x7Fu);
    EXPECT_TRUE(response.payload[62] == 0xFFu);

    {
        static const uint8_t position_payload[] = {
            0xFFu, 0xFFu, 0x80u, 0x00u,
            0x00u, 0x02u, 0x00u, 0x00u,
            0x00u, 0x03u, 0x00u, 0x00u,
            0x00u, 0x64u,
            0x00u, 0x00u, 0x13u, 0x88u};

        wire_length = encode_native_request(
            NATIVE_PROTOCOL_DEFAULT_DEVICE_ADDRESS,
            37u,
            NATIVE_PROTOCOL_MESSAGE_REQUEST,
            NATIVE_PROTOCOL_COMMAND_START_POSITION_RELATIVE,
            position_payload,
            sizeof(position_payload),
            wire,
            sizeof(wire));
        native_protocol_server_consume(&server, wire, wire_length);
        EXPECT_TRUE(native_protocol_decode_wire_frame(
                        transmit.bytes,
                        transmit.length,
                        &response) == NATIVE_PROTOCOL_DECODE_OK);
        EXPECT_TRUE(commissioning.position_start_calls == 1u);
        EXPECT_TRUE(
            commissioning.requested_position_displacement_revolutions_q16_16 ==
            -32768);
        EXPECT_TRUE(
            commissioning.requested_position_maximum_velocity_q16_16 ==
            2 * 65536);
        EXPECT_TRUE(
            commissioning.requested_position_maximum_acceleration_q16_16 ==
            3 * 65536);
        EXPECT_TRUE(
            commissioning.requested_position_current_limit_counts == 100u);
        EXPECT_TRUE(
            commissioning.requested_position_duration_millis == 5000u);
        EXPECT_TRUE(response.payload_length == 1u);
        EXPECT_TRUE(response.payload[0] == NATIVE_PROTOCOL_STATUS_OK);
    }

    {
        static const uint8_t expected_position_response[] = {
            0x00u, 0x01u, 0x02u, 0x00u, 0xFFu,
            0x01u, 0x02u, 0x03u, 0x04u,
            0x00u, 0x01u, 0x80u, 0x00u,
            0x00u, 0x01u, 0x40u, 0x00u,
            0xFFu, 0xFFu, 0x80u, 0x00u,
            0x00u, 0x00u, 0x40u, 0x00u,
            0xFFu, 0xFFu, 0xC0u, 0x00u,
            0x00u, 0x00u, 0x20u, 0x00u,
            0xFFu, 0xE7u, 0xFFu, 0xE8u, 0x00u, 0x64u,
            0x01u, 0x02u, 0x03u, 0x04u,
            0x05u, 0x06u, 0x07u, 0x08u,
            0x00u, 0x64u, 0x00u, 0x00u,
            0x00u, 0x04u, 0x00u, 0x00u,
            0x00u, 0x04u, 0x00u, 0x00u,
            0x00u, 0x00u, 0x40u, 0x00u};

        wire_length = encode_native_request(
            NATIVE_PROTOCOL_DEFAULT_DEVICE_ADDRESS,
            38u,
            NATIVE_PROTOCOL_MESSAGE_REQUEST,
            NATIVE_PROTOCOL_COMMAND_GET_POSITION_STATUS,
            NULL,
            0u,
            wire,
            sizeof(wire));
        native_protocol_server_consume(&server, wire, wire_length);
        EXPECT_TRUE(native_protocol_decode_wire_frame(
                        transmit.bytes,
                        transmit.length,
                        &response) == NATIVE_PROTOCOL_DECODE_OK);
        EXPECT_TRUE(commissioning.position_status_calls == 1u);
        EXPECT_TRUE(response.payload_length ==
                    sizeof(expected_position_response));
        EXPECT_TRUE(memcmp(response.payload,
                           expected_position_response,
                           sizeof(expected_position_response)) == 0);
    }
}

static void test_native_protocol_rejects_bad_crc_and_resynchronizes(void)
{
    uint8_t wire[NATIVE_PROTOCOL_MAX_WIRE_FRAME_SIZE];
    native_protocol_server_t server;
    mock_protocol_tx_t transmit = {.accept = true};
    native_protocol_stats_t stats;
    size_t wire_length;

    EXPECT_TRUE(init_native_server(&server, &transmit));
    wire_length = encode_native_request(
        NATIVE_PROTOCOL_DEFAULT_DEVICE_ADDRESS,
        3u,
        NATIVE_PROTOCOL_MESSAGE_REQUEST,
        NATIVE_PROTOCOL_COMMAND_GET_CAPABILITIES,
        NULL,
        0u,
        wire,
        sizeof(wire));
    EXPECT_TRUE(wire_length > 3u);
    wire[2] ^= (wire[2] == 1u) ? 2u : 1u;
    native_protocol_server_consume(&server, wire, wire_length);
    EXPECT_TRUE(transmit.call_count == 0u);

    wire_length = encode_native_request(
        NATIVE_PROTOCOL_DEFAULT_DEVICE_ADDRESS,
        4u,
        NATIVE_PROTOCOL_MESSAGE_REQUEST,
        NATIVE_PROTOCOL_COMMAND_GET_CAPABILITIES,
        NULL,
        0u,
        wire,
        sizeof(wire));
    native_protocol_server_consume(&server, wire, wire_length);
    native_protocol_server_get_stats(&server, &stats);
    EXPECT_TRUE(transmit.call_count == 1u);
    EXPECT_TRUE(stats.crc_errors == 1u);
    EXPECT_TRUE(stats.valid_frames == 1u);
}

static void test_native_protocol_discards_oversize_then_resynchronizes(void)
{
    uint8_t oversize[NATIVE_PROTOCOL_MAX_ENCODED_FRAME_SIZE + 2u];
    uint8_t wire[NATIVE_PROTOCOL_MAX_WIRE_FRAME_SIZE];
    native_protocol_server_t server;
    mock_protocol_tx_t transmit = {.accept = true};
    native_protocol_stats_t stats;
    size_t wire_length;
    size_t index;

    EXPECT_TRUE(init_native_server(&server, &transmit));
    for (index = 0u; index < (sizeof(oversize) - 1u); ++index)
    {
        oversize[index] = 0x7Fu;
    }
    oversize[sizeof(oversize) - 1u] = 0u;
    native_protocol_server_consume(&server, oversize, sizeof(oversize));

    wire_length = encode_native_request(
        NATIVE_PROTOCOL_DEFAULT_DEVICE_ADDRESS,
        5u,
        NATIVE_PROTOCOL_MESSAGE_REQUEST,
        NATIVE_PROTOCOL_COMMAND_PING,
        NULL,
        0u,
        wire,
        sizeof(wire));
    native_protocol_server_consume(&server, wire, wire_length);
    native_protocol_server_get_stats(&server, &stats);
    EXPECT_TRUE(stats.length_errors == 1u);
    EXPECT_TRUE(transmit.call_count == 1u);
}

static void test_native_protocol_rejects_bad_cobs_and_unknown_version(void)
{
    static const uint8_t malformed_cobs[] = {0x03u, 0x11u, 0x00u};
    uint8_t wire[NATIVE_PROTOCOL_MAX_WIRE_FRAME_SIZE];
    native_protocol_server_t server;
    mock_protocol_tx_t transmit = {.accept = true};
    native_protocol_stats_t stats;
    native_protocol_frame_t frame = {
        .version = 2u,
        .device_address = NATIVE_PROTOCOL_DEFAULT_DEVICE_ADDRESS,
        .sequence = 12u,
        .message_type = NATIVE_PROTOCOL_MESSAGE_REQUEST,
        .command = NATIVE_PROTOCOL_COMMAND_PING,
        .payload_length = 0u,
    };
    const size_t wire_length = native_protocol_encode_wire_frame(
        &frame,
        wire,
        sizeof(wire));

    EXPECT_TRUE(init_native_server(&server, &transmit));
    native_protocol_server_consume(
        &server,
        malformed_cobs,
        sizeof(malformed_cobs));
    native_protocol_server_consume(&server, wire, wire_length);
    native_protocol_server_get_stats(&server, &stats);
    EXPECT_TRUE(transmit.call_count == 0u);
    EXPECT_TRUE(stats.cobs_errors == 1u);
    EXPECT_TRUE(stats.version_errors == 1u);
}

static void test_native_protocol_suppresses_foreign_broadcast_and_response(void)
{
    uint8_t wire[NATIVE_PROTOCOL_MAX_WIRE_FRAME_SIZE];
    native_protocol_server_t server;
    mock_protocol_tx_t transmit = {.accept = true};
    native_protocol_stats_t stats;
    size_t wire_length;

    EXPECT_TRUE(init_native_server(&server, &transmit));
    wire_length = encode_native_request(
        2u,
        6u,
        NATIVE_PROTOCOL_MESSAGE_REQUEST,
        NATIVE_PROTOCOL_COMMAND_PING,
        NULL,
        0u,
        wire,
        sizeof(wire));
    native_protocol_server_consume(&server, wire, wire_length);

    wire_length = encode_native_request(
        NATIVE_PROTOCOL_BROADCAST_ADDRESS,
        7u,
        NATIVE_PROTOCOL_MESSAGE_REQUEST,
        NATIVE_PROTOCOL_COMMAND_PING,
        NULL,
        0u,
        wire,
        sizeof(wire));
    native_protocol_server_consume(&server, wire, wire_length);

    wire_length = encode_native_request(
        NATIVE_PROTOCOL_DEFAULT_DEVICE_ADDRESS,
        8u,
        NATIVE_PROTOCOL_MESSAGE_RESPONSE,
        NATIVE_PROTOCOL_COMMAND_PING,
        NULL,
        0u,
        wire,
        sizeof(wire));
    native_protocol_server_consume(&server, wire, wire_length);

    native_protocol_server_get_stats(&server, &stats);
    EXPECT_TRUE(transmit.call_count == 0u);
    EXPECT_TRUE(stats.ignored_addresses == 1u);
    EXPECT_TRUE(stats.broadcasts_dropped == 1u);
    EXPECT_TRUE(stats.unexpected_message_types == 1u);
}

static void test_native_protocol_returns_bounded_command_errors(void)
{
    static const uint8_t invalid_payload = 1u;
    uint8_t wire[NATIVE_PROTOCOL_MAX_WIRE_FRAME_SIZE];
    native_protocol_server_t server;
    mock_protocol_tx_t transmit = {.accept = true};
    native_protocol_frame_t response;
    size_t wire_length;

    EXPECT_TRUE(init_native_server(&server, &transmit));
    wire_length = encode_native_request(
        NATIVE_PROTOCOL_DEFAULT_DEVICE_ADDRESS,
        9u,
        NATIVE_PROTOCOL_MESSAGE_REQUEST,
        0x7777u,
        NULL,
        0u,
        wire,
        sizeof(wire));
    native_protocol_server_consume(&server, wire, wire_length);
    EXPECT_TRUE(native_protocol_decode_wire_frame(
                    transmit.bytes,
                    transmit.length,
                    &response) == NATIVE_PROTOCOL_DECODE_OK);
    EXPECT_TRUE(response.payload_length == 1u);
    EXPECT_TRUE(response.payload[0] ==
                NATIVE_PROTOCOL_STATUS_UNKNOWN_COMMAND);

    wire_length = encode_native_request(
        NATIVE_PROTOCOL_DEFAULT_DEVICE_ADDRESS,
        10u,
        NATIVE_PROTOCOL_MESSAGE_REQUEST,
        NATIVE_PROTOCOL_COMMAND_GET_IDENTITY,
        &invalid_payload,
        1u,
        wire,
        sizeof(wire));
    native_protocol_server_consume(&server, wire, wire_length);
    EXPECT_TRUE(native_protocol_decode_wire_frame(
                    transmit.bytes,
                    transmit.length,
                    &response) == NATIVE_PROTOCOL_DECODE_OK);
    EXPECT_TRUE(response.payload_length == 1u);
    EXPECT_TRUE(response.payload[0] ==
                NATIVE_PROTOCOL_STATUS_INVALID_PAYLOAD);
}

static void test_native_protocol_counts_transport_rejection(void)
{
    uint8_t wire[NATIVE_PROTOCOL_MAX_WIRE_FRAME_SIZE];
    native_protocol_server_t server;
    mock_protocol_tx_t transmit = {.accept = false};
    native_protocol_stats_t stats;
    const size_t wire_length = encode_native_request(
        NATIVE_PROTOCOL_DEFAULT_DEVICE_ADDRESS,
        11u,
        NATIVE_PROTOCOL_MESSAGE_REQUEST,
        NATIVE_PROTOCOL_COMMAND_PING,
        NULL,
        0u,
        wire,
        sizeof(wire));

    EXPECT_TRUE(init_native_server(&server, &transmit));
    native_protocol_server_consume(&server, wire, wire_length);
    native_protocol_server_get_stats(&server, &stats);
    EXPECT_TRUE(transmit.call_count == 1u);
    EXPECT_TRUE(stats.responses_sent == 0u);
    EXPECT_TRUE(stats.transmit_rejections == 1u);
}

static void test_angle_tracker_unwraps_in_both_directions(void)
{
    const angle_tracker_config_t config = {
        .counts_per_revolution = 16384u,
        .maximum_sample_interval_us = 2000u,
        .maximum_velocity_revolutions_per_second = 20.0f,
        .velocity_filter_alpha = 1.0f,
    };
    angle_tracker_t tracker;

    EXPECT_TRUE(angle_tracker_init(&tracker, &config));
    EXPECT_TRUE(!tracker.initialized);
    EXPECT_TRUE(angle_tracker_config_is_valid(&tracker.config));
    EXPECT_TRUE(angle_tracker_push(&tracker, 16380u, 0u));
    EXPECT_TRUE(tracker.initialized);
    EXPECT_TRUE(angle_tracker_push(&tracker, 4u, 1000u));
    EXPECT_TRUE(tracker.position_revolutions > 1.0f);
    EXPECT_TRUE(tracker.position_revolutions < 1.001f);
    EXPECT_TRUE(tracker.velocity_revolutions_per_second > 0.0f);

    EXPECT_TRUE(angle_tracker_push(&tracker, 16380u, 2000u));
    EXPECT_TRUE(fabsf(tracker.position_revolutions -
                      (16380.0f / 16384.0f)) < 0.0001f);
    EXPECT_TRUE(tracker.velocity_revolutions_per_second < 0.0f);
}

static void test_angle_tracker_rejects_implausible_motion_without_advancing(void)
{
    const angle_tracker_config_t config = {
        .counts_per_revolution = 16384u,
        .maximum_sample_interval_us = 2000u,
        .maximum_velocity_revolutions_per_second = 2.0f,
        .velocity_filter_alpha = 0.5f,
    };
    angle_tracker_t tracker;

    EXPECT_TRUE(angle_tracker_init(&tracker, &config));
    EXPECT_TRUE(angle_tracker_push(&tracker, 100u, 1000u));
    EXPECT_TRUE(!angle_tracker_push(&tracker, 8000u, 2000u));
    EXPECT_TRUE(tracker.last_raw_angle == 100u);
    EXPECT_TRUE(tracker.last_timestamp_us == 1000u);
}

static void test_encoder_liveness_requires_fresh_progress(void)
{
    encoder_liveness_monitor_t monitor;

    EXPECT_TRUE(!encoder_liveness_monitor_init(NULL, 3000u));
    EXPECT_TRUE(!encoder_liveness_monitor_init(&monitor, 0u));
    EXPECT_TRUE(!encoder_liveness_monitor_init(
        &monitor, (uint32_t)INT32_MAX + 1u));
    EXPECT_TRUE(encoder_liveness_monitor_init(&monitor, 3000u));
    EXPECT_TRUE(!encoder_liveness_monitor_is_live(&monitor));
    EXPECT_TRUE(!encoder_liveness_monitor_update(
        &monitor, false, 0u, 0u, 0u));

    EXPECT_TRUE(encoder_liveness_monitor_update(
        &monitor, true, 1u, 1000u, 1000u));
    EXPECT_TRUE(encoder_liveness_monitor_update(
        &monitor, true, 1u, 1000u, 4000u));
    EXPECT_TRUE(!encoder_liveness_monitor_update(
        &monitor, true, 1u, 1000u, 4001u));

    /* A stale monitor cannot appear healthy after a full timer wrap. */
    EXPECT_TRUE(!encoder_liveness_monitor_update(
        &monitor, true, 1u, 1000u, 1000u));
    EXPECT_TRUE(encoder_liveness_monitor_update(
        &monitor, true, 2u, 1000u, 1000u));
}

static void test_encoder_liveness_handles_counter_and_timer_wrap(void)
{
    encoder_liveness_monitor_t monitor;
    const uint32_t sample_timestamp = UINT32_MAX - 1000u;

    EXPECT_TRUE(encoder_liveness_monitor_init(&monitor, 3000u));
    EXPECT_TRUE(encoder_liveness_monitor_update(
        &monitor,
        true,
        UINT32_MAX,
        sample_timestamp,
        sample_timestamp));
    EXPECT_TRUE(encoder_liveness_monitor_update(
        &monitor, true, UINT32_MAX, sample_timestamp, 1000u));
    EXPECT_TRUE(!encoder_liveness_monitor_update(
        &monitor, true, UINT32_MAX, sample_timestamp, 2000u));

    EXPECT_TRUE(encoder_liveness_monitor_update(
        &monitor, true, 0u, 2000u, 2000u));
}

static electrical_phase_predictor_config_t
test_electrical_phase_predictor_config(void)
{
    const electrical_phase_predictor_config_t config = {
        .electrical_cycles_per_mechanical_revolution = 50u,
        .output_lead_us = 7u,
        .maximum_prediction_age_us = 2000u,
        .maximum_mechanical_velocity_q16_16 = 5 << 16,
    };

    return config;
}

static void test_electrical_phase_predictor_advances_at_current_loop_rate(void)
{
    const electrical_phase_predictor_config_t config =
        test_electrical_phase_predictor_config();
    electrical_phase_predictor_t predictor;
    uint32_t phase_at_sample = 0u;
    uint32_t phase_after_one_fast_step = 0u;
    uint32_t phase_after_one_encoder_period = 0u;
    uint32_t age_us = 0u;
    uint32_t fast_step_delta;
    uint32_t encoder_period_delta;

    EXPECT_TRUE(electrical_phase_predictor_config_is_valid(&config));
    EXPECT_TRUE(electrical_phase_predictor_init(&predictor, &config));
    EXPECT_TRUE(electrical_phase_predictor_set_observation(
        &predictor, 0x10000000u, 4 << 16, 1, 1000u));
    EXPECT_TRUE(electrical_phase_predictor_predict(
        &predictor, 1000u, &phase_at_sample, &age_us));
    EXPECT_TRUE(age_us == 0u);
    EXPECT_TRUE((phase_at_sample - 0x10000000u) > 0x005BB000u);
    EXPECT_TRUE((phase_at_sample - 0x10000000u) < 0x005BD000u);
    EXPECT_TRUE(electrical_phase_predictor_predict(
        &predictor, 1050u, &phase_after_one_fast_step, &age_us));
    EXPECT_TRUE(age_us == 50u);
    EXPECT_TRUE(electrical_phase_predictor_predict(
        &predictor, 2000u, &phase_after_one_encoder_period, &age_us));
    EXPECT_TRUE(age_us == 1000u);

    fast_step_delta = phase_after_one_fast_step - phase_at_sample;
    encoder_period_delta =
        phase_after_one_encoder_period - phase_at_sample;
    /* 4 mechanical rev/s * 50 cycles/rev advances 3.6 electrical degrees
       per 50 us current-loop period and 72 degrees per 1 ms encoder period. */
    EXPECT_TRUE(fast_step_delta > 0x028F5900u);
    EXPECT_TRUE(fast_step_delta < 0x028F6000u);
    EXPECT_TRUE(encoder_period_delta > 0x33333000u);
    EXPECT_TRUE(encoder_period_delta < 0x33333600u);
}

static void test_electrical_phase_predictor_handles_direction_age_and_wrap(void)
{
    electrical_phase_predictor_config_t config =
        test_electrical_phase_predictor_config();
    electrical_phase_predictor_t predictor;
    uint32_t forward_phase = 0u;
    uint32_t reverse_phase = 0u;
    uint32_t boundary_phase = 0u;
    uint32_t age_us = 0u;
    const uint32_t timestamp_us = UINT32_MAX - 500u;

    EXPECT_TRUE(electrical_phase_predictor_init(&predictor, &config));
    EXPECT_TRUE(!electrical_phase_predictor_predict(
        &predictor, 0u, &forward_phase, &age_us));
    EXPECT_TRUE(!electrical_phase_predictor_set_observation(
        &predictor, 0u, 4 << 16, 0, timestamp_us));
    EXPECT_TRUE(!electrical_phase_predictor_set_observation(
        &predictor, 0u, (5 << 16) + 1, 1, timestamp_us));

    EXPECT_TRUE(electrical_phase_predictor_set_observation(
        &predictor, 0u, 4 << 16, 1, timestamp_us));
    EXPECT_TRUE(electrical_phase_predictor_predict(
        &predictor, 499u, &forward_phase, &age_us));
    EXPECT_TRUE(age_us == 1000u);
    EXPECT_TRUE(electrical_phase_predictor_predict(
        &predictor, 1499u, &boundary_phase, &age_us));
    EXPECT_TRUE(age_us == 2000u);
    EXPECT_TRUE(!electrical_phase_predictor_predict(
        &predictor, 1500u, &forward_phase, &age_us));
    EXPECT_TRUE(age_us == 2001u);

    EXPECT_TRUE(electrical_phase_predictor_set_observation(
        &predictor, 0u, 4 << 16, -1, 1000u));
    EXPECT_TRUE(electrical_phase_predictor_predict(
        &predictor, 2000u, &reverse_phase, &age_us));
    EXPECT_TRUE((forward_phase + reverse_phase) < 0x00001000u);

    electrical_phase_predictor_reset(&predictor);
    EXPECT_TRUE(!electrical_phase_predictor_predict(
        &predictor, 2000u, &reverse_phase, &age_us));

    config.output_lead_us = 2001u;
    EXPECT_TRUE(!electrical_phase_predictor_config_is_valid(&config));
    config = test_electrical_phase_predictor_config();
    config.maximum_prediction_age_us = (uint32_t)INT32_MAX + 1u;
    EXPECT_TRUE(!electrical_phase_predictor_config_is_valid(&config));
    EXPECT_TRUE(!electrical_phase_predictor_init(NULL, &config));
}

static void test_motor_alignment_accepts_measured_stepper_geometry(void)
{
    const motor_alignment_config_t config = {
        .encoder_counts_per_revolution = 16384u,
        .electrical_cycles_per_revolution = 50u,
        .maximum_quarter_step_error_counts = 12u,
    };
    motor_alignment_t alignment;
    motor_alignment_status_t status;
    uint32_t phase_q32 = 0u;

    EXPECT_TRUE(motor_alignment_init(&alignment, &config));
    EXPECT_TRUE(!motor_alignment_electrical_phase_q32(
        &alignment, 14249u, &phase_q32));
    EXPECT_TRUE(motor_alignment_calibrate(
        &alignment, 14249u, 14165u));
    motor_alignment_get_status(&alignment, &status);
    EXPECT_TRUE(status.valid);
    EXPECT_TRUE(status.electrical_zero_raw == 14249u);
    EXPECT_TRUE(status.observed_quarter_step_counts == 84u);
    EXPECT_TRUE(status.quarter_step_error_counts == 2);
    EXPECT_TRUE(status.encoder_direction == -1);

    EXPECT_TRUE(motor_alignment_electrical_phase_q32(
        &alignment, 14249u, &phase_q32));
    EXPECT_TRUE(phase_q32 == 0u);
    EXPECT_TRUE(motor_alignment_electrical_phase_q32(
        &alignment, 14085u, &phase_q32));
    EXPECT_TRUE(phase_q32 > 0x7F000000u);
    EXPECT_TRUE(phase_q32 < 0x81000000u);

    EXPECT_TRUE(!motor_alignment_calibrate(
        &alignment, 1000u, 1400u));
    motor_alignment_get_status(&alignment, &status);
    EXPECT_TRUE(status.valid);
    EXPECT_TRUE(status.electrical_zero_raw == 14249u);

    motor_alignment_clear(&alignment);
    EXPECT_TRUE(!motor_alignment_calibrate(
        &alignment, 1000u, 1400u));
    motor_alignment_get_status(&alignment, &status);
    EXPECT_TRUE(!status.valid);

    {
        motor_alignment_t restored;
        motor_alignment_status_t persisted = {
            .electrical_zero_raw = 9302u,
            .observed_quarter_step_counts = 80u,
            .quarter_step_error_counts = -2,
            .encoder_direction = -1,
            .valid = true,
        };

        EXPECT_TRUE(motor_alignment_init(&restored, &config));
        EXPECT_TRUE(motor_alignment_restore(&restored, &persisted));
        motor_alignment_get_status(&restored, &status);
        EXPECT_TRUE(status.valid);
        EXPECT_TRUE(status.electrical_zero_raw == 9302u);
        EXPECT_TRUE(status.observed_quarter_step_counts == 80u);
        EXPECT_TRUE(status.quarter_step_error_counts == -2);
        EXPECT_TRUE(status.encoder_direction == -1);

        persisted.quarter_step_error_counts = 3;
        EXPECT_TRUE(!motor_alignment_restore(&restored, &persisted));
        motor_alignment_get_status(&restored, &status);
        EXPECT_TRUE(status.electrical_zero_raw == 9302u);
    }
}

static product_configuration_t test_product_configuration(void)
{
    const product_configuration_t configuration = {
        .encoder_counts_per_revolution = 16384u,
        .electrical_cycles_per_revolution = 50u,
        .alignment = {
            .electrical_zero_raw = 9302u,
            .observed_quarter_step_counts = 80u,
            .quarter_step_error_counts = -2,
            .encoder_direction = -1,
            .valid = true,
        },
    };

    return configuration;
}

static void test_configuration_store_persists_and_avoids_unchanged_writes(void)
{
    mock_configuration_flash_t flash;
    configuration_store_t store;
    configuration_store_t reloaded;
    product_configuration_t configuration = test_product_configuration();
    product_configuration_t loaded;
    configuration_store_backend_t backend;

    mock_configuration_flash_init(&flash);
    backend = mock_configuration_backend(&flash);
    EXPECT_TRUE(configuration_store_init(&store, &backend) ==
                CONFIGURATION_STORE_RESULT_EMPTY);
    EXPECT_TRUE(!configuration_store_get(&store, &loaded));
    EXPECT_TRUE(configuration_store_save(&store, &configuration) ==
                CONFIGURATION_STORE_RESULT_OK);
    EXPECT_TRUE(store.active_slot == 0u);
    EXPECT_TRUE(store.generation == 1u);
    EXPECT_TRUE(flash.erase_calls == 1u);
    EXPECT_TRUE(flash.program_calls == 8u);

    EXPECT_TRUE(configuration_store_save(&store, &configuration) ==
                CONFIGURATION_STORE_RESULT_OK);
    EXPECT_TRUE(flash.erase_calls == 1u);
    EXPECT_TRUE(flash.program_calls == 8u);

    EXPECT_TRUE(configuration_store_init(&reloaded, &backend) ==
                CONFIGURATION_STORE_RESULT_OK);
    EXPECT_TRUE(configuration_store_get(&reloaded, &loaded));
    EXPECT_TRUE(configuration_store_matches(&reloaded, &configuration));
    EXPECT_TRUE(loaded.alignment.valid);
    EXPECT_TRUE(loaded.alignment.electrical_zero_raw == 9302u);
    EXPECT_TRUE(loaded.alignment.encoder_direction == -1);
}

static void test_configuration_store_interrupted_update_keeps_old_slot(void)
{
    mock_configuration_flash_t flash;
    configuration_store_t store;
    configuration_store_t reloaded;
    product_configuration_t original = test_product_configuration();
    product_configuration_t updated = original;
    product_configuration_t loaded;
    configuration_store_backend_t backend;

    mock_configuration_flash_init(&flash);
    backend = mock_configuration_backend(&flash);
    EXPECT_TRUE(configuration_store_init(&store, &backend) ==
                CONFIGURATION_STORE_RESULT_EMPTY);
    EXPECT_TRUE(configuration_store_save(&store, &original) ==
                CONFIGURATION_STORE_RESULT_OK);

    updated.alignment.electrical_zero_raw = 9304u;
    flash.fail_program_call = 16u;
    EXPECT_TRUE(configuration_store_save(&store, &updated) ==
                CONFIGURATION_STORE_RESULT_IO_ERROR);
    EXPECT_TRUE(store.active_slot == 0u);
    EXPECT_TRUE(store.generation == 1u);

    flash.fail_program_call = 0u;
    EXPECT_TRUE(configuration_store_init(&reloaded, &backend) ==
                CONFIGURATION_STORE_RESULT_OK);
    EXPECT_TRUE(configuration_store_get(&reloaded, &loaded));
    EXPECT_TRUE(loaded.alignment.electrical_zero_raw ==
                original.alignment.electrical_zero_raw);
    EXPECT_TRUE(reloaded.active_slot == 0u);
    EXPECT_TRUE(reloaded.valid_slot_mask == 1u);
}

static void test_configuration_store_crc_fallback_and_persistent_clear(void)
{
    mock_configuration_flash_t flash;
    configuration_store_t store;
    configuration_store_t reloaded;
    product_configuration_t original = test_product_configuration();
    product_configuration_t updated = original;
    product_configuration_t cleared = {0};
    product_configuration_t loaded;
    configuration_store_backend_t backend;

    mock_configuration_flash_init(&flash);
    backend = mock_configuration_backend(&flash);
    EXPECT_TRUE(configuration_store_init(&store, &backend) ==
                CONFIGURATION_STORE_RESULT_EMPTY);
    EXPECT_TRUE(configuration_store_save(&store, &original) ==
                CONFIGURATION_STORE_RESULT_OK);
    updated.alignment.electrical_zero_raw = 9304u;
    EXPECT_TRUE(configuration_store_save(&store, &updated) ==
                CONFIGURATION_STORE_RESULT_OK);
    EXPECT_TRUE(store.active_slot == 1u);
    EXPECT_TRUE(store.generation == 2u);

    flash.words[1][4] ^= 1u;
    EXPECT_TRUE(configuration_store_init(&reloaded, &backend) ==
                CONFIGURATION_STORE_RESULT_OK);
    EXPECT_TRUE(configuration_store_get(&reloaded, &loaded));
    EXPECT_TRUE(reloaded.active_slot == 0u);
    EXPECT_TRUE(loaded.alignment.electrical_zero_raw == 9302u);

    mock_configuration_flash_init(&flash);
    backend = mock_configuration_backend(&flash);
    EXPECT_TRUE(configuration_store_init(&store, &backend) ==
                CONFIGURATION_STORE_RESULT_EMPTY);
    EXPECT_TRUE(configuration_store_save(&store, &original) ==
                CONFIGURATION_STORE_RESULT_OK);
    cleared.encoder_counts_per_revolution = 16384u;
    cleared.electrical_cycles_per_revolution = 50u;
    EXPECT_TRUE(configuration_store_save(&store, &cleared) ==
                CONFIGURATION_STORE_RESULT_OK);
    EXPECT_TRUE(configuration_store_init(&reloaded, &backend) ==
                CONFIGURATION_STORE_RESULT_OK);
    EXPECT_TRUE(configuration_store_get(&reloaded, &loaded));
    EXPECT_TRUE(reloaded.generation == 2u);
    EXPECT_TRUE(!loaded.alignment.valid);
}

static alignment_controller_config_t test_alignment_controller_config(void)
{
    const alignment_controller_config_t config = {
        .settle_duration_millis = 10u,
        .sample_duration_millis = 4u,
        .maximum_duration_millis = 100u,
        .minimum_sample_count = 3u,
        .maximum_sample_span_counts = 4u,
        .maximum_closure_error_counts = 4u,
        .maximum_current_error_counts = 2u,
    };

    return config;
}

static motor_alignment_config_t test_motor_alignment_config(void)
{
    const motor_alignment_config_t config = {
        .encoder_counts_per_revolution = 16384u,
        .electrical_cycles_per_revolution = 50u,
        .maximum_quarter_step_error_counts = 12u,
    };

    return config;
}

static void test_alignment_controller_commits_closed_sequence(void)
{
    const alignment_controller_config_t controller_config =
        test_alignment_controller_config();
    const motor_alignment_config_t alignment_config =
        test_motor_alignment_config();
    alignment_controller_t controller;
    alignment_controller_status_t controller_status;
    motor_alignment_t alignment;
    motor_alignment_status_t alignment_status;
    int16_t reference_a = 0;
    int16_t reference_b = 0;

    EXPECT_TRUE(alignment_controller_init(
        &controller, &controller_config));
    EXPECT_TRUE(motor_alignment_init(&alignment, &alignment_config));
    EXPECT_TRUE(alignment_controller_start(
        &controller, &alignment, 125u, 0u));
    EXPECT_TRUE(alignment_controller_get_reference_counts(
        &controller, &reference_a, &reference_b));
    EXPECT_TRUE(reference_a == 125);
    EXPECT_TRUE(reference_b == 0);

    EXPECT_TRUE(alignment_controller_update(
        &controller, 10u, true, 14249u, 125, 0, true) ==
        ALIGNMENT_CONTROLLER_EVENT_NONE);
    EXPECT_TRUE(alignment_controller_update(
        &controller, 12u, true, 14250u, 124, 1, true) ==
        ALIGNMENT_CONTROLLER_EVENT_NONE);
    EXPECT_TRUE(alignment_controller_update(
        &controller, 14u, true, 14249u, 125, 0, true) ==
        ALIGNMENT_CONTROLLER_EVENT_REFERENCE_CHANGED);
    EXPECT_TRUE(alignment_controller_get_reference_counts(
        &controller, &reference_a, &reference_b));
    EXPECT_TRUE(reference_a == 0);
    EXPECT_TRUE(reference_b == 125);

    EXPECT_TRUE(alignment_controller_update(
        &controller, 24u, true, 14165u, 0, 125, true) ==
        ALIGNMENT_CONTROLLER_EVENT_NONE);
    EXPECT_TRUE(alignment_controller_update(
        &controller, 26u, true, 14165u, 1, 124, true) ==
        ALIGNMENT_CONTROLLER_EVENT_NONE);
    EXPECT_TRUE(alignment_controller_update(
        &controller, 28u, true, 14164u, 0, 125, true) ==
        ALIGNMENT_CONTROLLER_EVENT_REFERENCE_CHANGED);
    EXPECT_TRUE(alignment_controller_get_reference_counts(
        &controller, &reference_a, &reference_b));
    EXPECT_TRUE(reference_a == 125);
    EXPECT_TRUE(reference_b == 0);

    EXPECT_TRUE(alignment_controller_update(
        &controller, 38u, true, 14250u, 125, 0, true) ==
        ALIGNMENT_CONTROLLER_EVENT_NONE);
    EXPECT_TRUE(alignment_controller_update(
        &controller, 40u, true, 14249u, 124, 0, true) ==
        ALIGNMENT_CONTROLLER_EVENT_NONE);
    EXPECT_TRUE(alignment_controller_update(
        &controller, 42u, true, 14250u, 125, 1, true) ==
        ALIGNMENT_CONTROLLER_EVENT_COMPLETED);

    alignment_controller_get_status(&controller, &controller_status);
    motor_alignment_get_status(&alignment, &alignment_status);
    EXPECT_TRUE(controller_status.state ==
                ALIGNMENT_CONTROLLER_STATE_COMPLETE);
    EXPECT_TRUE(controller_status.result ==
                ALIGNMENT_CONTROLLER_RESULT_SUCCESS);
    EXPECT_TRUE(controller_status.phase_zero_raw == 14249u);
    EXPECT_TRUE(controller_status.phase_quarter_raw == 14165u);
    EXPECT_TRUE(controller_status.return_zero_raw == 14250u);
    EXPECT_TRUE(controller_status.closure_error_counts == 1);
    EXPECT_TRUE(!alignment_controller_is_active(&controller));
    EXPECT_TRUE(alignment_status.valid);
    EXPECT_TRUE(alignment_status.encoder_direction == -1);
    EXPECT_TRUE(alignment_controller_get_reference_counts(
        &controller, &reference_a, &reference_b));
    EXPECT_TRUE(reference_a == 0);
    EXPECT_TRUE(reference_b == 0);
}

static void test_alignment_controller_failure_preserves_calibration(void)
{
    const alignment_controller_config_t controller_config =
        test_alignment_controller_config();
    const motor_alignment_config_t alignment_config =
        test_motor_alignment_config();
    alignment_controller_t controller;
    alignment_controller_status_t controller_status;
    motor_alignment_t alignment;
    motor_alignment_status_t alignment_status;

    EXPECT_TRUE(alignment_controller_init(
        &controller, &controller_config));
    EXPECT_TRUE(motor_alignment_init(&alignment, &alignment_config));
    EXPECT_TRUE(motor_alignment_calibrate(
        &alignment, 14249u, 14165u));
    EXPECT_TRUE(alignment_controller_start(
        &controller, &alignment, 125u, 100u));

    EXPECT_TRUE(alignment_controller_update(
        &controller, 110u, true, 1000u, 125, 0, true) ==
        ALIGNMENT_CONTROLLER_EVENT_NONE);
    EXPECT_TRUE(alignment_controller_update(
        &controller, 112u, true, 1000u, 125, 0, true) ==
        ALIGNMENT_CONTROLLER_EVENT_NONE);
    EXPECT_TRUE(alignment_controller_update(
        &controller, 114u, true, 1000u, 125, 0, true) ==
        ALIGNMENT_CONTROLLER_EVENT_REFERENCE_CHANGED);
    EXPECT_TRUE(alignment_controller_update(
        &controller, 124u, true, 1400u, 0, 125, true) ==
        ALIGNMENT_CONTROLLER_EVENT_NONE);
    EXPECT_TRUE(alignment_controller_update(
        &controller, 126u, true, 1400u, 0, 125, true) ==
        ALIGNMENT_CONTROLLER_EVENT_NONE);
    EXPECT_TRUE(alignment_controller_update(
        &controller, 128u, true, 1400u, 0, 125, true) ==
        ALIGNMENT_CONTROLLER_EVENT_REFERENCE_CHANGED);
    EXPECT_TRUE(alignment_controller_update(
        &controller, 138u, true, 1000u, 125, 0, true) ==
        ALIGNMENT_CONTROLLER_EVENT_NONE);
    EXPECT_TRUE(alignment_controller_update(
        &controller, 140u, true, 1000u, 125, 0, true) ==
        ALIGNMENT_CONTROLLER_EVENT_NONE);
    EXPECT_TRUE(alignment_controller_update(
        &controller, 142u, true, 1000u, 125, 0, true) ==
        ALIGNMENT_CONTROLLER_EVENT_FAILED);

    alignment_controller_get_status(&controller, &controller_status);
    motor_alignment_get_status(&alignment, &alignment_status);
    EXPECT_TRUE(controller_status.result ==
                ALIGNMENT_CONTROLLER_RESULT_GEOMETRY);
    EXPECT_TRUE(alignment_status.valid);
    EXPECT_TRUE(alignment_status.electrical_zero_raw == 14249u);
}

static void test_alignment_controller_aborts_and_rejects_bad_feedback(void)
{
    const alignment_controller_config_t controller_config =
        test_alignment_controller_config();
    const motor_alignment_config_t alignment_config =
        test_motor_alignment_config();
    alignment_controller_t controller;
    alignment_controller_status_t status;
    motor_alignment_t alignment;

    EXPECT_TRUE(alignment_controller_init(
        &controller, &controller_config));
    EXPECT_TRUE(motor_alignment_init(&alignment, &alignment_config));
    EXPECT_TRUE(alignment_controller_start(
        &controller, &alignment, 125u, UINT32_MAX - 5u));
    alignment_controller_abort(&controller, 2u);
    alignment_controller_get_status(&controller, &status);
    EXPECT_TRUE(status.state == ALIGNMENT_CONTROLLER_STATE_ABORTED);
    EXPECT_TRUE(status.result == ALIGNMENT_CONTROLLER_RESULT_ABORTED);
    EXPECT_TRUE(status.elapsed_millis == 8u);

    EXPECT_TRUE(alignment_controller_start(
        &controller, &alignment, 125u, 10u));
    EXPECT_TRUE(alignment_controller_update(
        &controller, 20u, true, 14249u, 100, 0, true) ==
        ALIGNMENT_CONTROLLER_EVENT_FAILED);
    alignment_controller_get_status(&controller, &status);
    EXPECT_TRUE(status.result ==
                ALIGNMENT_CONTROLLER_RESULT_CURRENT_TRACKING);

    EXPECT_TRUE(alignment_controller_start(
        &controller, &alignment, 125u, 30u));
    EXPECT_TRUE(alignment_controller_update(
        &controller, 40u, false, 0u, 125, 0, true) ==
        ALIGNMENT_CONTROLLER_EVENT_FAILED);
    alignment_controller_get_status(&controller, &status);
    EXPECT_TRUE(status.result ==
                ALIGNMENT_CONTROLLER_RESULT_ENCODER_INVALID);
}

static void test_alignment_controller_rejects_runtime_failures(void)
{
    const alignment_controller_config_t controller_config =
        test_alignment_controller_config();
    const motor_alignment_config_t alignment_config =
        test_motor_alignment_config();
    alignment_controller_t controller;
    alignment_controller_status_t status;
    motor_alignment_t alignment;

    EXPECT_TRUE(alignment_controller_init(
        &controller, &controller_config));
    EXPECT_TRUE(motor_alignment_init(&alignment, &alignment_config));

    EXPECT_TRUE(alignment_controller_start(
        &controller, &alignment, 125u, 0u));
    EXPECT_TRUE(alignment_controller_update(
        &controller, 1u, true, 14249u, 125, 0, false) ==
        ALIGNMENT_CONTROLLER_EVENT_FAILED);
    alignment_controller_get_status(&controller, &status);
    EXPECT_TRUE(status.result ==
                ALIGNMENT_CONTROLLER_RESULT_BACKEND_INACTIVE);

    EXPECT_TRUE(alignment_controller_start(
        &controller, &alignment, 125u, 100u));
    EXPECT_TRUE(alignment_controller_update(
        &controller, 201u, true, 14249u, 125, 0, true) ==
        ALIGNMENT_CONTROLLER_EVENT_FAILED);
    alignment_controller_get_status(&controller, &status);
    EXPECT_TRUE(status.result ==
                ALIGNMENT_CONTROLLER_RESULT_DEADLINE);

    EXPECT_TRUE(alignment_controller_start(
        &controller, &alignment, 125u, 0u));
    EXPECT_TRUE(alignment_controller_update(
        &controller, 10u, true, 1000u, 125, 0, true) ==
        ALIGNMENT_CONTROLLER_EVENT_NONE);
    EXPECT_TRUE(alignment_controller_update(
        &controller, 12u, true, 1005u, 125, 0, true) ==
        ALIGNMENT_CONTROLLER_EVENT_FAILED);
    alignment_controller_get_status(&controller, &status);
    EXPECT_TRUE(status.result ==
                ALIGNMENT_CONTROLLER_RESULT_ENCODER_UNSTABLE);

    EXPECT_TRUE(alignment_controller_start(
        &controller, &alignment, 125u, 0u));
    EXPECT_TRUE(alignment_controller_update(
        &controller, 10u, true, 1000u, 125, 0, true) ==
        ALIGNMENT_CONTROLLER_EVENT_NONE);
    EXPECT_TRUE(alignment_controller_update(
        &controller, 12u, true, 1000u, 125, 0, true) ==
        ALIGNMENT_CONTROLLER_EVENT_NONE);
    EXPECT_TRUE(alignment_controller_update(
        &controller, 14u, true, 1000u, 125, 0, true) ==
        ALIGNMENT_CONTROLLER_EVENT_REFERENCE_CHANGED);
    EXPECT_TRUE(alignment_controller_update(
        &controller, 24u, true, 918u, 0, 125, true) ==
        ALIGNMENT_CONTROLLER_EVENT_NONE);
    EXPECT_TRUE(alignment_controller_update(
        &controller, 26u, true, 918u, 0, 125, true) ==
        ALIGNMENT_CONTROLLER_EVENT_NONE);
    EXPECT_TRUE(alignment_controller_update(
        &controller, 28u, true, 918u, 0, 125, true) ==
        ALIGNMENT_CONTROLLER_EVENT_REFERENCE_CHANGED);
    EXPECT_TRUE(alignment_controller_update(
        &controller, 38u, true, 1010u, 125, 0, true) ==
        ALIGNMENT_CONTROLLER_EVENT_NONE);
    EXPECT_TRUE(alignment_controller_update(
        &controller, 40u, true, 1010u, 125, 0, true) ==
        ALIGNMENT_CONTROLLER_EVENT_NONE);
    EXPECT_TRUE(alignment_controller_update(
        &controller, 42u, true, 1010u, 125, 0, true) ==
        ALIGNMENT_CONTROLLER_EVENT_FAILED);
    alignment_controller_get_status(&controller, &status);
    EXPECT_TRUE(status.result == ALIGNMENT_CONTROLLER_RESULT_CLOSURE);
}

static void test_motion_profile_respects_velocity_and_acceleration_limits(void)
{
    const motion_profile_config_t config = {
        .maximum_velocity_revolutions_per_second = 2.0f,
        .maximum_acceleration_revolutions_per_second_squared = 4.0f,
        .maximum_step_seconds = 0.002f,
        .position_tolerance_revolutions = 0.0005f,
        .velocity_tolerance_revolutions_per_second = 0.002f,
    };
    motion_profile_t profile;
    float previous_velocity = 0.0f;
    unsigned int iteration;

    EXPECT_TRUE(motion_profile_init(&profile, 0.0f));
    EXPECT_TRUE(motion_profile_set_target(&profile, 1.0f));
    for (iteration = 0u; iteration < 4000u; ++iteration)
    {
        EXPECT_TRUE(motion_profile_step(&profile, &config, 0.001f));
        EXPECT_TRUE(fabsf(profile.velocity_revolutions_per_second) <=
                    2.0001f);
        EXPECT_TRUE(fabsf(profile.velocity_revolutions_per_second -
                          previous_velocity) <= 0.0041f);
        previous_velocity = profile.velocity_revolutions_per_second;
    }
    EXPECT_TRUE(motion_profile_is_settled(&profile, &config));
    EXPECT_TRUE(fabsf(profile.position_revolutions - 1.0f) < 0.001f);
}

static void test_motion_profile_controlled_stop_decelerates_to_rest(void)
{
    const motion_profile_config_t config = {
        .maximum_velocity_revolutions_per_second = 2.0f,
        .maximum_acceleration_revolutions_per_second_squared = 4.0f,
        .maximum_step_seconds = 0.002f,
        .position_tolerance_revolutions = 0.0005f,
        .velocity_tolerance_revolutions_per_second = 0.002f,
    };
    motion_profile_t profile;
    float position_when_stop_requested;
    float stop_target;
    unsigned int iteration;

    EXPECT_TRUE(motion_profile_init(&profile, 0.0f));
    EXPECT_TRUE(motion_profile_set_target(&profile, 10.0f));
    for (iteration = 0u; iteration < 500u; ++iteration)
    {
        EXPECT_TRUE(motion_profile_step(&profile, &config, 0.001f));
    }
    position_when_stop_requested = profile.position_revolutions;
    EXPECT_TRUE(profile.velocity_revolutions_per_second > 1.9f);
    EXPECT_TRUE(motion_profile_request_stop(&profile, &config));
    stop_target = profile.target_position_revolutions;
    EXPECT_TRUE(stop_target > position_when_stop_requested);

    for (iteration = 0u; iteration < 2000u; ++iteration)
    {
        EXPECT_TRUE(motion_profile_step(&profile, &config, 0.001f));
    }
    EXPECT_TRUE(motion_profile_is_settled(&profile, &config));
    EXPECT_TRUE(fabsf(profile.position_revolutions - stop_target) < 0.001f);
    EXPECT_TRUE(profile.velocity_revolutions_per_second == 0.0f);
}

static void test_pi_controller_prevents_integrator_windup(void)
{
    const pi_controller_config_t config = {
        .proportional_gain = 2.0f,
        .integral_gain_per_second = 10.0f,
        .output_limit = 1.0f,
        .integrator_limit = 2.0f,
    };
    pi_controller_t controller;
    float output = 0.0f;
    unsigned int iteration;

    pi_controller_reset(&controller);
    for (iteration = 0u; iteration < 100u; ++iteration)
    {
        EXPECT_TRUE(pi_controller_step(&controller,
                                       &config,
                                       10.0f,
                                       0.01f,
                                       &output));
        EXPECT_TRUE(output == 1.0f);
    }
    EXPECT_TRUE(controller.integrator == 0.0f);
    EXPECT_TRUE(pi_controller_step(&controller,
                                   &config,
                                   -0.1f,
                                   0.1f,
                                   &output));
    EXPECT_TRUE(output < 0.0f);
    EXPECT_TRUE(controller.integrator < 0.0f);
}

static void test_servo_core_latches_stale_rotor_feedback(void)
{
    const servo_core_config_t config = test_servo_config();
    servo_core_t core;
    servo_core_output_t output;

    EXPECT_TRUE(servo_core_init(&core, &config));
    EXPECT_TRUE(observe_servo(&core, 0.0f, 0.0f, 0u) ==
                SERVO_CORE_STATUS_OK);
    EXPECT_TRUE(servo_core_step(&core, 1000u, &output) ==
                SERVO_CORE_STATUS_OK);
    EXPECT_TRUE(servo_core_step(&core, 2000u, &output) ==
                SERVO_CORE_STATUS_OK);
    EXPECT_TRUE(servo_core_step(&core, 4000u, &output) ==
                SERVO_CORE_STATUS_FAULTED);
    EXPECT_TRUE(!output.valid);
    EXPECT_TRUE(output.torque_current_request_amperes == 0.0f);
    EXPECT_TRUE((output.fault_flags & SERVO_FAULT_STALE_FEEDBACK) != 0u);
}

static void test_servo_core_rejects_invalid_rotor_observations(void)
{
    const servo_core_config_t config = test_servo_config();
    const rotor_observation_t duplicate_timestamp = {
        .position_revolutions = 0.1f,
        .velocity_revolutions_per_second = 1.0f,
        .timestamp_us = 0u,
        .valid = true,
    };
    servo_core_t core;

    EXPECT_TRUE(servo_core_init(&core, &config));
    EXPECT_TRUE(observe_servo(&core, 0.0f, 0.0f, 0u) ==
                SERVO_CORE_STATUS_OK);
    EXPECT_TRUE(servo_core_observe_rotor(&core, &duplicate_timestamp) ==
                SERVO_CORE_STATUS_FAULTED);
    EXPECT_TRUE((core.fault_flags & SERVO_FAULT_INVALID_FEEDBACK) != 0u);
    EXPECT_TRUE(core.feedback_position_revolutions == 0.0f);
    EXPECT_TRUE(core.last_feedback_timestamp_us == 0u);
}

static void test_servo_core_latches_following_error(void)
{
    servo_core_config_t config = test_servo_config();
    servo_core_t core;
    servo_core_output_t output;
    uint32_t timestamp_us = 0u;
    unsigned int iteration;

    config.maximum_following_error_revolutions = 0.005f;
    EXPECT_TRUE(servo_core_init(&core, &config));
    EXPECT_TRUE(observe_servo(&core, 0.0f, 0.0f, timestamp_us) ==
                SERVO_CORE_STATUS_OK);
    EXPECT_TRUE(servo_core_set_position_target(&core, 1.0f) ==
                SERVO_CORE_STATUS_OK);

    for (iteration = 0u;
         (iteration < 500u) && !servo_core_is_faulted(&core);
         ++iteration)
    {
        timestamp_us += 1000u;
        EXPECT_TRUE(observe_servo(&core, 0.0f, 0.0f, timestamp_us) ==
                    SERVO_CORE_STATUS_OK);
        (void)servo_core_step(&core, timestamp_us, &output);
    }

    EXPECT_TRUE(servo_core_is_faulted(&core));
    EXPECT_TRUE((core.fault_flags & SERVO_FAULT_FOLLOWING_ERROR) != 0u);
}

static void test_servo_core_closes_position_loop_against_simple_plant(void)
{
    const servo_core_config_t config = test_servo_config();
    servo_core_t core;
    servo_core_output_t output;
    float plant_position = 0.0f;
    float plant_velocity = 0.0f;
    float maximum_current = 0.0f;
    uint32_t timestamp_us = 0u;
    unsigned int iteration;

    EXPECT_TRUE(servo_core_init(&core, &config));
    EXPECT_TRUE(observe_servo(&core, 0.0f, 0.0f, timestamp_us) ==
                SERVO_CORE_STATUS_OK);
    EXPECT_TRUE(servo_core_set_position_target(&core, 1.0f) ==
                SERVO_CORE_STATUS_OK);

    for (iteration = 0u; iteration < 8000u; ++iteration)
    {
        float acceleration;

        timestamp_us += 1000u;
        EXPECT_TRUE(observe_servo(&core,
                                  plant_position,
                                  plant_velocity,
                                  timestamp_us) == SERVO_CORE_STATUS_OK);
        EXPECT_TRUE(servo_core_step(&core, timestamp_us, &output) ==
                    SERVO_CORE_STATUS_OK);
        EXPECT_TRUE(output.valid);
        EXPECT_TRUE(fabsf(output.torque_current_request_amperes) <=
                    config.maximum_current_amperes);

        if (fabsf(output.torque_current_request_amperes) > maximum_current)
        {
            maximum_current =
                fabsf(output.torque_current_request_amperes);
        }
        acceleration =
            (8.0f * output.torque_current_request_amperes) -
            (2.0f * plant_velocity);
        plant_velocity += acceleration * 0.001f;
        plant_position += plant_velocity * 0.001f;
    }

    EXPECT_TRUE(!servo_core_is_faulted(&core));
    EXPECT_TRUE(maximum_current <= config.maximum_current_amperes);
    EXPECT_TRUE(fabsf(plant_position - 1.0f) < 0.02f);
    EXPECT_TRUE(fabsf(plant_velocity) < 0.05f);
}

static void test_velocity_controller_tracks_bounded_simple_plant(void)
{
    const velocity_controller_config_t config =
        test_velocity_controller_config();
    velocity_controller_t controller;
    velocity_controller_status_t status;
    rotor_observation_t observation = {
        .position_revolutions = 0.0f,
        .velocity_revolutions_per_second = 0.0f,
        .timestamp_us = 0u,
        .valid = true,
    };
    float plant_position = 0.0f;
    float plant_velocity = 0.0f;
    int16_t requested_current = 0;
    uint32_t step;

    EXPECT_TRUE(velocity_controller_config_is_valid(&config));
    EXPECT_TRUE(velocity_controller_init(&controller, &config));
    EXPECT_TRUE(velocity_controller_start(
        &controller, 1 << 14, 50u, 5000u, 1, 0u, &observation));

    for (step = 1u; step <= 3000u; ++step)
    {
        float acceleration;

        observation.position_revolutions = plant_position;
        observation.velocity_revolutions_per_second = plant_velocity;
        observation.timestamp_us = step * 1000u;
        EXPECT_TRUE(velocity_controller_update(
            &controller,
            step,
            &observation,
            &requested_current) ==
            VELOCITY_CONTROL_EVENT_CURRENT_CHANGED);
        EXPECT_TRUE((requested_current >= -50) &&
                    (requested_current <= 50));
        acceleration = (0.08f * (float)requested_current) -
                       (1.5f * plant_velocity);
        plant_velocity += acceleration * 0.001f;
        plant_position += plant_velocity * 0.001f;
    }

    velocity_controller_get_status(&controller, &status);
    EXPECT_TRUE(status.state == VELOCITY_CONTROL_STATE_TRACKING);
    EXPECT_TRUE(status.current_limit_counts == 50u);
    EXPECT_TRUE(fabsf(plant_velocity - 0.25f) < 0.02f);
    EXPECT_TRUE(velocity_controller_stop(&controller, 3001u));
    velocity_controller_get_status(&controller, &status);
    EXPECT_TRUE(status.result == VELOCITY_CONTROL_RESULT_STOPPED);
    EXPECT_TRUE(status.requested_q_current_counts == 0);
}

static void test_velocity_controller_rejects_bounds_and_faults_feedback(void)
{
    velocity_controller_config_t config =
        test_velocity_controller_config();
    velocity_controller_t controller;
    velocity_controller_status_t status;
    rotor_observation_t observation = {
        .position_revolutions = 0.0f,
        .velocity_revolutions_per_second = 0.0f,
        .timestamp_us = 1000u,
        .valid = true,
    };
    int16_t requested_current = 123;

    config.maximum_duration_millis = UINT32_C(0x80000000);
    EXPECT_TRUE(!velocity_controller_config_is_valid(&config));
    config = test_velocity_controller_config();
    EXPECT_TRUE(velocity_controller_init(&controller, &config));
    EXPECT_TRUE(!velocity_controller_start(
        &controller, 0, 25u, 100u, 1, 0u, &observation));
    EXPECT_TRUE(!velocity_controller_start(
        &controller, (1 << 16) + 1, 25u, 100u, 1, 0u, &observation));
    EXPECT_TRUE(!velocity_controller_start(
        &controller, 1 << 15, 101u, 100u, 1, 0u, &observation));
    EXPECT_TRUE(!velocity_controller_start(
        &controller, 1 << 15, 25u, 100u, 0, 0u, &observation));
    EXPECT_TRUE(velocity_controller_start(
        &controller, 1 << 15, 25u, 100u, 1, 0u, &observation));
    EXPECT_TRUE(velocity_controller_update(
        &controller, 1u, &observation, &requested_current) ==
        VELOCITY_CONTROL_EVENT_FAILED);
    EXPECT_TRUE(requested_current == 0);
    velocity_controller_get_status(&controller, &status);
    EXPECT_TRUE(status.result == VELOCITY_CONTROL_RESULT_FEEDBACK_TIMING);
    EXPECT_TRUE((status.fault_flags &
                 VELOCITY_CONTROL_FAULT_FEEDBACK_TIMING) != 0u);

    EXPECT_TRUE(velocity_controller_start(
        &controller, -(1 << 15), 25u, 100u, 1, 10u, &observation));
    observation.timestamp_us = 2000u;
    observation.velocity_revolutions_per_second = 5.01f;
    EXPECT_TRUE(velocity_controller_update(
        &controller, 11u, &observation, &requested_current) ==
        VELOCITY_CONTROL_EVENT_FAILED);
    velocity_controller_get_status(&controller, &status);
    EXPECT_TRUE(status.result == VELOCITY_CONTROL_RESULT_OVERSPEED);
}

static void test_velocity_controller_deadline_clears_current(void)
{
    const velocity_controller_config_t config =
        test_velocity_controller_config();
    velocity_controller_t controller;
    velocity_controller_status_t status;
    rotor_observation_t observation = {
        .position_revolutions = 0.0f,
        .velocity_revolutions_per_second = 0.0f,
        .timestamp_us = 0u,
        .valid = true,
    };
    int16_t requested_current = 0;

    EXPECT_TRUE(velocity_controller_init(&controller, &config));
    EXPECT_TRUE(velocity_controller_start(
        &controller, 1 << 14, 25u, 3u, 1, 100u, &observation));
    observation.timestamp_us = 1000u;
    EXPECT_TRUE(velocity_controller_update(
        &controller, 101u, &observation, &requested_current) ==
        VELOCITY_CONTROL_EVENT_CURRENT_CHANGED);
    observation.timestamp_us = 2000u;
    EXPECT_TRUE(velocity_controller_update(
        &controller, 102u, &observation, &requested_current) ==
        VELOCITY_CONTROL_EVENT_CURRENT_CHANGED);
    observation.timestamp_us = 3000u;
    requested_current = 123;
    EXPECT_TRUE(velocity_controller_update(
        &controller, 103u, &observation, &requested_current) ==
        VELOCITY_CONTROL_EVENT_COMPLETED);
    EXPECT_TRUE(requested_current == 0);
    velocity_controller_get_status(&controller, &status);
    EXPECT_TRUE(status.result == VELOCITY_CONTROL_RESULT_DEADLINE);
    EXPECT_TRUE(!velocity_controller_is_active(&controller));
}

static void test_velocity_controller_limits_current_and_recovers(void)
{
    const velocity_controller_config_t config =
        test_velocity_controller_config();
    velocity_controller_t controller;
    velocity_controller_status_t status;
    rotor_observation_t observation = {
        .position_revolutions = 0.0f,
        .velocity_revolutions_per_second = 0.0f,
        .timestamp_us = 1000u,
        .valid = true,
    };
    int16_t requested_current = 0;
    uint32_t step;

    EXPECT_TRUE(velocity_controller_init(&controller, &config));
    EXPECT_TRUE(velocity_controller_start(
        &controller, 1 << 16, 10u, 1000u, 1, 0u, &observation));
    for (step = 1u; step <= 100u; ++step)
    {
        observation.timestamp_us = 1000u + step * 1000u;
        EXPECT_TRUE(velocity_controller_update(
            &controller,
            step,
            &observation,
            &requested_current) ==
            VELOCITY_CONTROL_EVENT_CURRENT_CHANGED);
        EXPECT_TRUE(requested_current <= 10);
    }
    velocity_controller_get_status(&controller, &status);
    EXPECT_TRUE(status.requested_q_current_counts == 10);
    EXPECT_TRUE(status.reference_velocity_revolutions_per_second_q16_16 >=
                6553);
    EXPECT_TRUE(status.reference_velocity_revolutions_per_second_q16_16 <=
                6554);

    observation.velocity_revolutions_per_second =
        controller.reference_velocity_revolutions_per_second;
    observation.timestamp_us += 1000u;
    EXPECT_TRUE(velocity_controller_update(
        &controller,
        101u,
        &observation,
        &requested_current) == VELOCITY_CONTROL_EVENT_CURRENT_CHANGED);
    EXPECT_TRUE((requested_current >= -1) && (requested_current <= 1));
    EXPECT_TRUE(velocity_controller_stop(&controller, 102u));

    observation.velocity_revolutions_per_second = 0.0f;
    observation.timestamp_us += 1000u;
    EXPECT_TRUE(velocity_controller_start(
        &controller, -(1 << 15), 10u, 1000u, 1, 200u, &observation));
    for (step = 1u; step <= 10u; ++step)
    {
        observation.timestamp_us += 1000u;
        EXPECT_TRUE(velocity_controller_update(
            &controller,
            200u + step,
            &observation,
            &requested_current) == VELOCITY_CONTROL_EVENT_CURRENT_CHANGED);
    }
    EXPECT_TRUE(requested_current < 0);
    EXPECT_TRUE(requested_current >= -10);
}

static void test_velocity_controller_applies_alignment_direction(void)
{
    const velocity_controller_config_t config =
        test_velocity_controller_config();
    velocity_controller_t controller;
    velocity_controller_status_t status;
    rotor_observation_t observation = {
        .position_revolutions = 0.0f,
        .velocity_revolutions_per_second = 0.0f,
        .timestamp_us = 0u,
        .valid = true,
    };
    int16_t requested_current = 0;
    uint32_t step;

    EXPECT_TRUE(velocity_controller_init(&controller, &config));
    EXPECT_TRUE(velocity_controller_start(
        &controller, 1 << 16, 25u, 1000u, -1, 0u, &observation));
    for (step = 1u; step <= 20u; ++step)
    {
        observation.timestamp_us = step * 1000u;
        EXPECT_TRUE(velocity_controller_update(
            &controller,
            step,
            &observation,
            &requested_current) ==
            VELOCITY_CONTROL_EVENT_CURRENT_CHANGED);
    }
    velocity_controller_get_status(&controller, &status);
    EXPECT_TRUE(requested_current < 0);
    EXPECT_TRUE(status.requested_q_current_counts == requested_current);
    EXPECT_TRUE(velocity_controller_stop(&controller, 21u));
}

static void test_velocity_controller_accepts_dynamic_tracking_targets(void)
{
    const velocity_controller_config_t config =
        test_velocity_controller_config();
    velocity_controller_t controller;
    velocity_controller_status_t status;
    rotor_observation_t observation = {
        .position_revolutions = 0.0f,
        .velocity_revolutions_per_second = 0.0f,
        .timestamp_us = 0u,
        .valid = true,
    };
    int16_t requested_current = 0;
    uint32_t step;

    EXPECT_TRUE(velocity_controller_init(&controller, &config));
    EXPECT_TRUE(velocity_controller_start_tracking(
        &controller, 0, 25u, 1000u, 1, 0u, &observation));
    EXPECT_TRUE(velocity_controller_set_target(&controller, 1 << 15));
    EXPECT_TRUE(!velocity_controller_set_target(
        &controller, (1 << 16) + 1));
    observation.timestamp_us = 1000u;
    EXPECT_TRUE(velocity_controller_update(
        &controller, 1u, &observation, &requested_current) ==
        VELOCITY_CONTROL_EVENT_CURRENT_CHANGED);
    velocity_controller_get_status(&controller, &status);
    EXPECT_TRUE(status.target_velocity_revolutions_per_second_q16_16 ==
                1 << 15);
    EXPECT_TRUE(status.reference_velocity_revolutions_per_second_q16_16 >=
                65);
    EXPECT_TRUE(status.reference_velocity_revolutions_per_second_q16_16 <=
                66);
    for (step = 2u; step <= 20u; ++step)
    {
        observation.timestamp_us = step * 1000u;
        EXPECT_TRUE(velocity_controller_update(
            &controller, step, &observation, &requested_current) ==
            VELOCITY_CONTROL_EVENT_CURRENT_CHANGED);
    }
    EXPECT_TRUE(requested_current > 0);
}

static void test_position_controller_profiles_and_settles(void)
{
    const position_controller_config_t config =
        test_position_controller_config();
    position_controller_t controller;
    position_controller_status_t status;
    rotor_observation_t observation = {
        .position_revolutions = 0.0f,
        .velocity_revolutions_per_second = 0.0f,
        .timestamp_us = 0u,
        .valid = true,
    };
    int32_t target_velocity = 0;
    position_control_event_t event = POSITION_CONTROL_EVENT_NONE;
    uint32_t step;

    EXPECT_TRUE(position_controller_config_is_valid(&config));
    EXPECT_TRUE(position_controller_init(&controller, &config));
    EXPECT_TRUE(position_controller_start_relative(
        &controller,
        1 << 14,
        1 << 16,
        2 << 16,
        50u,
        5000u,
        0u,
        &observation));

    for (step = 1u; step <= 3000u; ++step)
    {
        observation.position_revolutions =
            controller.profile.position_revolutions;
        observation.velocity_revolutions_per_second =
            controller.profile.velocity_revolutions_per_second;
        observation.timestamp_us = step * 1000u;
        event = position_controller_update(
            &controller, step, &observation, &target_velocity);
        EXPECT_TRUE((target_velocity >= -(1 << 16)) &&
                    (target_velocity <= (1 << 16)));
        if (event == POSITION_CONTROL_EVENT_COMPLETED)
        {
            break;
        }
        EXPECT_TRUE(event == POSITION_CONTROL_EVENT_VELOCITY_CHANGED);
    }

    position_controller_get_status(&controller, &status);
    EXPECT_TRUE(event == POSITION_CONTROL_EVENT_COMPLETED);
    EXPECT_TRUE(status.state == POSITION_CONTROL_STATE_COMPLETE);
    EXPECT_TRUE(status.result == POSITION_CONTROL_RESULT_SETTLED);
    EXPECT_TRUE(status.target_position_revolutions_q16_16 == 1 << 14);
    EXPECT_TRUE(status.current_limit_counts == 50u);
    EXPECT_TRUE(!position_controller_is_active(&controller));
}

static void test_position_controller_enforces_following_error_and_deadline(void)
{
    position_controller_config_t config =
        test_position_controller_config();
    position_controller_t controller;
    position_controller_status_t status;
    rotor_observation_t observation = {
        .position_revolutions = 0.0f,
        .velocity_revolutions_per_second = 0.0f,
        .timestamp_us = 0u,
        .valid = true,
    };
    int32_t target_velocity = 123;
    position_control_event_t event = POSITION_CONTROL_EVENT_NONE;
    uint32_t step;

    config.maximum_velocity_target_revolutions_per_second =
        config.maximum_velocity_revolutions_per_second;
    EXPECT_TRUE(!position_controller_config_is_valid(&config));
    config = test_position_controller_config();
    config.maximum_following_error_revolutions = 0.01f;
    EXPECT_TRUE(position_controller_init(&controller, &config));
    EXPECT_TRUE(position_controller_start_relative(
        &controller,
        1 << 16,
        1 << 16,
        4 << 16,
        50u,
        1000u,
        0u,
        &observation));
    for (step = 1u; step <= 200u; ++step)
    {
        observation.timestamp_us = step * 1000u;
        event = position_controller_update(
            &controller, step, &observation, &target_velocity);
        if (event == POSITION_CONTROL_EVENT_FAILED)
        {
            break;
        }
    }
    position_controller_get_status(&controller, &status);
    EXPECT_TRUE(event == POSITION_CONTROL_EVENT_FAILED);
    EXPECT_TRUE(status.result == POSITION_CONTROL_RESULT_FOLLOWING_ERROR);
    EXPECT_TRUE((status.fault_flags &
                 POSITION_CONTROL_FAULT_FOLLOWING_ERROR) != 0u);
    EXPECT_TRUE(target_velocity == 0);

    config = test_position_controller_config();
    EXPECT_TRUE(position_controller_init(&controller, &config));
    observation.timestamp_us = 0u;
    EXPECT_TRUE(position_controller_start_relative(
        &controller,
        1 << 16,
        1 << 16,
        1 << 16,
        50u,
        100u,
        0u,
        &observation));
    observation.timestamp_us = 1000u;
    target_velocity = 123;
    EXPECT_TRUE(position_controller_update(
        &controller, 100u, &observation, &target_velocity) ==
        POSITION_CONTROL_EVENT_COMPLETED);
    position_controller_get_status(&controller, &status);
    EXPECT_TRUE(status.result == POSITION_CONTROL_RESULT_DEADLINE);
    EXPECT_TRUE(target_velocity == 0);
}

static void test_position_controller_preserves_velocity_correction_headroom(void)
{
    const position_controller_config_t config =
        test_position_controller_config();
    position_controller_t controller;
    rotor_observation_t observation = {
        .position_revolutions = 0.0f,
        .velocity_revolutions_per_second = 0.0f,
        .timestamp_us = 0u,
        .valid = true,
    };
    int32_t target_velocity = 0;

    EXPECT_TRUE(position_controller_init(&controller, &config));
    EXPECT_TRUE(position_controller_start_relative(
        &controller,
        10 << 16,
        4 << 16,
        4 << 16,
        50u,
        5000u,
        0u,
        &observation));

    controller.profile.position_revolutions = 1.0f;
    controller.profile.velocity_revolutions_per_second = 4.0f;
    observation.position_revolutions = 0.8f;
    observation.velocity_revolutions_per_second = 4.0f;
    observation.timestamp_us = 1000u;
    EXPECT_TRUE(position_controller_update(
        &controller, 1u, &observation, &target_velocity) ==
        POSITION_CONTROL_EVENT_VELOCITY_CHANGED);
    EXPECT_TRUE(target_velocity > (4 << 16));
    EXPECT_TRUE(target_velocity <= (5 << 16));
}

static current_controller_config_t test_current_controller_config(void)
{
    const current_controller_config_t config = {
        .d_axis = {
            .proportional_gain = 2.0f,
            .integral_gain_per_second = 400.0f,
            .output_limit = 4.0f,
            .integrator_limit = 4.0f,
        },
        .q_axis = {
            .proportional_gain = 2.0f,
            .integral_gain_per_second = 400.0f,
            .output_limit = 4.0f,
            .integrator_limit = 4.0f,
        },
        .maximum_voltage_magnitude = 4.0f,
        .vector_anti_windup_gain_per_second = 100.0f,
    };

    return config;
}

static void test_park_transform_round_trip(void)
{
    const stationary_vector_t stationary = {
        .alpha = 1.25f,
        .beta = -0.75f,
    };
    rotating_vector_t rotating;
    stationary_vector_t recovered;

    EXPECT_TRUE(park_transform(stationary, 1.234f, &rotating));
    EXPECT_TRUE(inverse_park_transform(rotating, 1.234f, &recovered));
    EXPECT_TRUE(fabsf(recovered.alpha - stationary.alpha) < 0.0001f);
    EXPECT_TRUE(fabsf(recovered.beta - stationary.beta) < 0.0001f);
}

static void test_current_controller_limits_voltage_vector(void)
{
    const current_controller_config_t config =
        test_current_controller_config();
    current_controller_t controller;
    current_controller_output_t output;
    const stationary_vector_t measured = {0};
    const rotating_vector_t requested = {
        .d = 100.0f,
        .q = 100.0f,
    };

    EXPECT_TRUE(current_controller_init(&controller, &config));
    EXPECT_TRUE(current_controller_step(&controller,
                                        &config,
                                        measured,
                                        requested,
                                        0.7f,
                                        0.00005f,
                                        &output));
    EXPECT_TRUE(output.voltage_saturated);
    EXPECT_TRUE(hypotf(output.voltage_dq.d, output.voltage_dq.q) <=
                4.0001f);
    EXPECT_TRUE(hypotf(output.voltage_alpha_beta.alpha,
                       output.voltage_alpha_beta.beta) <= 4.0001f);
}

static void test_current_controller_regulates_simple_rl_plant(void)
{
    const current_controller_config_t config =
        test_current_controller_config();
    const rotating_vector_t requested = {
        .d = 0.0f,
        .q = 1.0f,
    };
    current_controller_t controller;
    current_controller_output_t output;
    stationary_vector_t current = {0};
    unsigned int iteration;

    EXPECT_TRUE(current_controller_init(&controller, &config));
    for (iteration = 0u; iteration < 5000u; ++iteration)
    {
        EXPECT_TRUE(current_controller_step(&controller,
                                            &config,
                                            current,
                                            requested,
                                            0.0f,
                                            0.00005f,
                                            &output));

        /* Independent alpha/beta 5 mH, 1 ohm winding model. */
        current.alpha +=
            ((output.voltage_alpha_beta.alpha - current.alpha) /
             0.005f) * 0.00005f;
        current.beta +=
            ((output.voltage_alpha_beta.beta - current.beta) /
             0.005f) * 0.00005f;
    }

    EXPECT_TRUE(fabsf(current.alpha) < 0.01f);
    EXPECT_TRUE(fabsf(current.beta - 1.0f) < 0.02f);
    EXPECT_TRUE(hypotf(output.voltage_alpha_beta.alpha,
                       output.voltage_alpha_beta.beta) <= 4.0001f);
}

static bool apply_motion_action_to_servo(servo_core_t* core,
                                         const motion_action_t* action)
{
    switch (action->kind)
    {
        case MOTION_ACTION_NONE:
        case MOTION_ACTION_ENABLE:
        case MOTION_ACTION_DISABLE:
            return true;

        case MOTION_ACTION_SET_POSITION_TARGET:
            return servo_core_set_position_target(
                       core,
                       action->position_revolutions) ==
                   SERVO_CORE_STATUS_OK;

        case MOTION_ACTION_REQUEST_CONTROLLED_STOP:
            return servo_core_request_stop(core) ==
                   SERVO_CORE_STATUS_OK;

        default:
            return false;
    }
}

static void test_motion_manager_enforces_authority_and_idempotency(void)
{
    const motion_manager_config_t config = {
        .remote_lease_timeout_us = 100000u,
        .allowed_motion_sources = MOTION_SOURCE_MASK_NATIVE |
                                  MOTION_SOURCE_MASK_MODBUS,
    };
    const motion_request_t enable = {
        .source = MOTION_SOURCE_NATIVE,
        .command_id = 1u,
        .kind = MOTION_COMMAND_ENABLE,
    };
    const motion_request_t move = {
        .source = MOTION_SOURCE_NATIVE,
        .command_id = 2u,
        .kind = MOTION_COMMAND_MOVE_ABSOLUTE,
        .position_revolutions = 1.0f,
    };
    motion_request_t request;
    motion_manager_t manager;
    motion_manager_status_t status;
    motion_action_t action;
    uint32_t lease_deadline;

    EXPECT_TRUE(motion_manager_init(&manager, &config, 0.0f));
    EXPECT_TRUE(motion_manager_submit(&manager,
                                      &move,
                                      0u,
                                      0.0f,
                                      &action) ==
                MOTION_SUBMIT_NOT_ENABLED);
    EXPECT_TRUE(motion_manager_submit(&manager,
                                      &enable,
                                      0u,
                                      0.0f,
                                      &action) ==
                MOTION_SUBMIT_ACCEPTED);
    EXPECT_TRUE(action.kind == MOTION_ACTION_ENABLE);
    lease_deadline = manager.lease_deadline_us;

    request = move;
    request.source = MOTION_SOURCE_MODBUS;
    EXPECT_TRUE(motion_manager_submit(&manager,
                                      &request,
                                      1000u,
                                      0.0f,
                                      &action) == MOTION_SUBMIT_BUSY);

    request.source = MOTION_SOURCE_LOCAL;
    EXPECT_TRUE(motion_manager_submit(&manager,
                                      &request,
                                      1000u,
                                      0.0f,
                                      &action) ==
                MOTION_SUBMIT_SOURCE_DISABLED);

    EXPECT_TRUE(motion_manager_submit(&manager,
                                      &move,
                                      1000u,
                                      0.0f,
                                      &action) ==
                MOTION_SUBMIT_ACCEPTED);
    EXPECT_TRUE(action.kind == MOTION_ACTION_SET_POSITION_TARGET);
    EXPECT_TRUE(action.position_revolutions == 1.0f);
    lease_deadline = manager.lease_deadline_us;

    EXPECT_TRUE(motion_manager_submit(&manager,
                                      &move,
                                      50000u,
                                      0.1f,
                                      &action) ==
                MOTION_SUBMIT_DUPLICATE);
    EXPECT_TRUE(action.kind == MOTION_ACTION_NONE);
    EXPECT_TRUE(manager.lease_deadline_us == lease_deadline);

    request = move;
    request.position_revolutions = 2.0f;
    EXPECT_TRUE(motion_manager_submit(&manager,
                                      &request,
                                      50000u,
                                      0.1f,
                                      &action) ==
                MOTION_SUBMIT_CONFLICT);

    EXPECT_TRUE(motion_manager_poll(&manager,
                                    60000u,
                                    true,
                                    false,
                                    &action));
    motion_manager_get_status(&manager, &status);
    EXPECT_TRUE(status.state == MOTION_STATE_READY);
    EXPECT_TRUE(status.authority == MOTION_SOURCE_NATIVE);
    EXPECT_TRUE(status.active_completion == MOTION_COMPLETION_NONE);
    EXPECT_TRUE(status.last_command_id == 2u);
    EXPECT_TRUE(status.last_completion == MOTION_COMPLETION_COMPLETED);
}

static void test_motion_manager_lease_expiry_stops_then_disables(void)
{
    const motion_manager_config_t config = {
        .remote_lease_timeout_us = 100000u,
        .allowed_motion_sources = MOTION_SOURCE_MASK_NATIVE,
    };
    const motion_request_t enable = {
        .source = MOTION_SOURCE_NATIVE,
        .command_id = 10u,
        .kind = MOTION_COMMAND_ENABLE,
    };
    const motion_request_t move = {
        .source = MOTION_SOURCE_NATIVE,
        .command_id = 11u,
        .kind = MOTION_COMMAND_MOVE_ABSOLUTE,
        .position_revolutions = 5.0f,
    };
    motion_manager_t manager;
    motion_manager_status_t status;
    motion_action_t action;

    EXPECT_TRUE(motion_manager_init(&manager, &config, 0.0f));
    EXPECT_TRUE(motion_manager_submit(&manager,
                                      &enable,
                                      0u,
                                      0.0f,
                                      &action) ==
                MOTION_SUBMIT_ACCEPTED);
    EXPECT_TRUE(motion_manager_submit(&manager,
                                      &move,
                                      0u,
                                      0.0f,
                                      &action) ==
                MOTION_SUBMIT_ACCEPTED);

    EXPECT_TRUE(motion_manager_poll(&manager,
                                    99999u,
                                    false,
                                    false,
                                    &action));
    EXPECT_TRUE(action.kind == MOTION_ACTION_NONE);
    EXPECT_TRUE(motion_manager_poll(&manager,
                                    100000u,
                                    false,
                                    false,
                                    &action));
    EXPECT_TRUE(action.kind == MOTION_ACTION_REQUEST_CONTROLLED_STOP);
    motion_manager_get_status(&manager, &status);
    EXPECT_TRUE(status.state == MOTION_STATE_STOPPING);
    EXPECT_TRUE(status.last_command_id == 11u);
    EXPECT_TRUE(status.last_completion ==
                MOTION_COMPLETION_ABORTED_LEASE);

    EXPECT_TRUE(motion_manager_poll(&manager,
                                    101000u,
                                    true,
                                    false,
                                    &action));
    EXPECT_TRUE(action.kind == MOTION_ACTION_DISABLE);
    motion_manager_get_status(&manager, &status);
    EXPECT_TRUE(status.state == MOTION_STATE_DISABLED);
    EXPECT_TRUE(status.authority == MOTION_SOURCE_NONE);
}

static void test_motion_manager_keepalive_is_explicit_and_retries_stay_safe(void)
{
    const motion_manager_config_t config = {
        .remote_lease_timeout_us = 100000u,
        .allowed_motion_sources = MOTION_SOURCE_MASK_NATIVE,
    };
    const motion_request_t enable = {
        .source = MOTION_SOURCE_NATIVE,
        .command_id = 30u,
        .kind = MOTION_COMMAND_ENABLE,
    };
    const motion_request_t move = {
        .source = MOTION_SOURCE_NATIVE,
        .command_id = 31u,
        .kind = MOTION_COMMAND_MOVE_ABSOLUTE,
        .position_revolutions = 5.0f,
    };
    const motion_request_t keepalive = {
        .source = MOTION_SOURCE_NATIVE,
        .command_id = 32u,
        .kind = MOTION_COMMAND_KEEPALIVE,
    };
    motion_manager_t manager;
    motion_manager_status_t status;
    motion_action_t action;
    uint32_t refreshed_deadline;

    EXPECT_TRUE(motion_manager_init(&manager, &config, 0.0f));
    EXPECT_TRUE(motion_manager_submit(&manager,
                                      &enable,
                                      0u,
                                      0.0f,
                                      &action) ==
                MOTION_SUBMIT_ACCEPTED);
    EXPECT_TRUE(motion_manager_submit(&manager,
                                      &move,
                                      1000u,
                                      0.0f,
                                      &action) ==
                MOTION_SUBMIT_ACCEPTED);
    EXPECT_TRUE(motion_manager_submit(&manager,
                                      &keepalive,
                                      50000u,
                                      0.1f,
                                      &action) ==
                MOTION_SUBMIT_ACCEPTED);
    EXPECT_TRUE(action.kind == MOTION_ACTION_NONE);
    refreshed_deadline = manager.lease_deadline_us;
    EXPECT_TRUE(refreshed_deadline == 150000u);

    EXPECT_TRUE(motion_manager_submit(&manager,
                                      &keepalive,
                                      60000u,
                                      0.1f,
                                      &action) ==
                MOTION_SUBMIT_DUPLICATE);
    EXPECT_TRUE(motion_manager_submit(&manager,
                                      &move,
                                      60000u,
                                      0.1f,
                                      &action) ==
                MOTION_SUBMIT_DUPLICATE);
    EXPECT_TRUE(manager.lease_deadline_us == refreshed_deadline);

    EXPECT_TRUE(motion_manager_poll(&manager,
                                    149999u,
                                    false,
                                    false,
                                    &action));
    EXPECT_TRUE(action.kind == MOTION_ACTION_NONE);
    EXPECT_TRUE(motion_manager_poll(&manager,
                                    150000u,
                                    false,
                                    false,
                                    &action));
    EXPECT_TRUE(action.kind == MOTION_ACTION_REQUEST_CONTROLLED_STOP);
    motion_manager_get_status(&manager, &status);
    EXPECT_TRUE(status.last_command_source == MOTION_SOURCE_NATIVE);
    EXPECT_TRUE(status.last_command_id == 31u);
    EXPECT_TRUE(status.last_completion ==
                MOTION_COMPLETION_ABORTED_LEASE);
    EXPECT_TRUE(status.previous_command_source == MOTION_SOURCE_NATIVE);
    EXPECT_TRUE(status.previous_command_id == 32u);
    EXPECT_TRUE(status.previous_completion == MOTION_COMPLETION_COMPLETED);
}

static void test_motion_manager_lease_deadline_survives_timestamp_wrap(void)
{
    const motion_manager_config_t config = {
        .remote_lease_timeout_us = 100u,
        .allowed_motion_sources = MOTION_SOURCE_MASK_NATIVE,
    };
    const motion_request_t enable = {
        .source = MOTION_SOURCE_NATIVE,
        .command_id = 40u,
        .kind = MOTION_COMMAND_ENABLE,
    };
    motion_manager_t manager;
    motion_action_t action;

    EXPECT_TRUE(motion_manager_init(&manager, &config, 0.0f));
    EXPECT_TRUE(motion_manager_submit(&manager,
                                      &enable,
                                      UINT32_MAX - 50u,
                                      0.0f,
                                      &action) ==
                MOTION_SUBMIT_ACCEPTED);
    EXPECT_TRUE(manager.lease_deadline_us == 49u);
    EXPECT_TRUE(motion_manager_poll(&manager,
                                    UINT32_MAX,
                                    false,
                                    false,
                                    &action));
    EXPECT_TRUE(action.kind == MOTION_ACTION_NONE);
    EXPECT_TRUE(motion_manager_poll(&manager,
                                    48u,
                                    false,
                                    false,
                                    &action));
    EXPECT_TRUE(action.kind == MOTION_ACTION_NONE);
    EXPECT_TRUE(motion_manager_poll(&manager,
                                    49u,
                                    false,
                                    false,
                                    &action));
    EXPECT_TRUE(action.kind == MOTION_ACTION_REQUEST_CONTROLLED_STOP);
}

static void test_motion_manager_allows_foreign_stop_and_latches_fault(void)
{
    const motion_manager_config_t config = {
        .remote_lease_timeout_us = 100000u,
        .allowed_motion_sources = MOTION_SOURCE_MASK_NATIVE,
    };
    motion_request_t request = {
        .source = MOTION_SOURCE_NATIVE,
        .command_id = 20u,
        .kind = MOTION_COMMAND_ENABLE,
    };
    motion_manager_t manager;
    motion_manager_status_t status;
    motion_action_t action;

    EXPECT_TRUE(motion_manager_init(&manager, &config, 0.0f));
    EXPECT_TRUE(motion_manager_submit(&manager,
                                      &request,
                                      0u,
                                      0.0f,
                                      &action) ==
                MOTION_SUBMIT_ACCEPTED);
    request.command_id = 21u;
    request.kind = MOTION_COMMAND_MOVE_ABSOLUTE;
    request.position_revolutions = 3.0f;
    EXPECT_TRUE(motion_manager_submit(&manager,
                                      &request,
                                      1000u,
                                      0.0f,
                                      &action) ==
                MOTION_SUBMIT_ACCEPTED);

    request.source = MOTION_SOURCE_LOCAL;
    request.command_id = 22u;
    request.kind = MOTION_COMMAND_STOP;
    EXPECT_TRUE(motion_manager_submit(&manager,
                                      &request,
                                      2000u,
                                      0.1f,
                                      &action) ==
                MOTION_SUBMIT_ACCEPTED);
    EXPECT_TRUE(action.kind == MOTION_ACTION_REQUEST_CONTROLLED_STOP);
    motion_manager_get_status(&manager, &status);
    EXPECT_TRUE(status.active_command_source == MOTION_SOURCE_LOCAL);
    EXPECT_TRUE(status.active_command_id == 22u);
    EXPECT_TRUE(status.last_command_source == MOTION_SOURCE_NATIVE);
    EXPECT_TRUE(status.last_command_id == 21u);
    EXPECT_TRUE(status.last_completion == MOTION_COMPLETION_ABORTED_STOP);

    EXPECT_TRUE(motion_manager_poll(&manager,
                                    3000u,
                                    false,
                                    true,
                                    &action));
    EXPECT_TRUE(action.kind == MOTION_ACTION_DISABLE);
    motion_manager_get_status(&manager, &status);
    EXPECT_TRUE(status.state == MOTION_STATE_FAULT);
    EXPECT_TRUE(status.last_command_source == MOTION_SOURCE_LOCAL);
    EXPECT_TRUE(status.last_command_id == 22u);
    EXPECT_TRUE(status.last_completion == MOTION_COMPLETION_FAULTED);
    EXPECT_TRUE(status.previous_command_source == MOTION_SOURCE_NATIVE);
    EXPECT_TRUE(status.previous_command_id == 21u);
    EXPECT_TRUE(status.previous_completion ==
                MOTION_COMPLETION_ABORTED_STOP);
    EXPECT_TRUE(!motion_manager_clear_fault(&manager, false, 0.1f));
    EXPECT_TRUE(motion_manager_clear_fault(&manager, true, 0.1f));
    motion_manager_get_status(&manager, &status);
    EXPECT_TRUE(status.state == MOTION_STATE_DISABLED);
}

static void test_step_direction_reanchors_and_tracks_signed_counts(void)
{
    const step_direction_config_t config = {
        .steps_per_revolution = 3200u,
        .maximum_sample_interval_us = 2000u,
        .maximum_step_rate_per_second = 160000.0f,
    };
    step_direction_t model;
    step_direction_output_t output;

    EXPECT_TRUE(step_direction_init(&model, &config));
    EXPECT_TRUE(step_direction_update(&model,
                                      100,
                                      false,
                                      0u,
                                      2.0f,
                                      &output));
    EXPECT_TRUE(output.event == STEP_DIRECTION_EVENT_NONE);
    EXPECT_TRUE(step_direction_update(&model,
                                      200,
                                      false,
                                      1000u,
                                      2.1f,
                                      &output));
    EXPECT_TRUE(output.target_position_revolutions == 2.1f);

    EXPECT_TRUE(step_direction_update(&model,
                                      200,
                                      true,
                                      2000u,
                                      2.2f,
                                      &output));
    EXPECT_TRUE(output.event == STEP_DIRECTION_EVENT_ENABLED);
    EXPECT_TRUE(output.target_position_revolutions == 2.2f);
    EXPECT_TRUE(step_direction_update(&model,
                                      360,
                                      true,
                                      3000u,
                                      2.2f,
                                      &output));
    EXPECT_TRUE(output.event == STEP_DIRECTION_EVENT_TARGET_UPDATED);
    EXPECT_TRUE(output.delta_steps == 160);
    EXPECT_TRUE(fabsf(output.target_position_revolutions - 2.25f) <
                0.0001f);
    EXPECT_TRUE(step_direction_update(&model,
                                      200,
                                      true,
                                      4000u,
                                      2.2f,
                                      &output));
    EXPECT_TRUE(output.delta_steps == -160);
    EXPECT_TRUE(fabsf(output.target_position_revolutions - 2.2f) <
                0.0001f);

    EXPECT_TRUE(step_direction_update(&model,
                                      200,
                                      false,
                                      5000u,
                                      2.18f,
                                      &output));
    EXPECT_TRUE(output.event == STEP_DIRECTION_EVENT_DISABLED);
    EXPECT_TRUE(step_direction_update(&model,
                                      500,
                                      true,
                                      6000u,
                                      2.19f,
                                      &output));
    EXPECT_TRUE(output.event == STEP_DIRECTION_EVENT_ENABLED);
    EXPECT_TRUE(fabsf(output.target_position_revolutions - 2.19f) <
                0.0001f);
}

static void test_step_direction_rejects_rate_and_handles_counter_wrap(void)
{
    const step_direction_config_t config = {
        .steps_per_revolution = 3200u,
        .maximum_sample_interval_us = 2000u,
        .maximum_step_rate_per_second = 160000.0f,
    };
    step_direction_t model;
    step_direction_output_t output;

    EXPECT_TRUE(step_direction_init(&model, &config));
    EXPECT_TRUE(step_direction_update(&model,
                                      INT32_MAX - 5,
                                      true,
                                      0u,
                                      0.0f,
                                      &output));
    EXPECT_TRUE(step_direction_update(&model,
                                      INT32_MIN + 4,
                                      true,
                                      1000u,
                                      0.0f,
                                      &output));
    EXPECT_TRUE(output.delta_steps == 10);

    EXPECT_TRUE(!step_direction_update(&model,
                                       INT32_MIN + 1004,
                                       true,
                                       2000u,
                                       0.0f,
                                       &output));
    EXPECT_TRUE(model.last_cumulative_steps == INT32_MIN + 4);
    EXPECT_TRUE(model.last_timestamp_us == 1000u);
}

static void test_motion_manager_reports_simulated_move_completion(void)
{
    const servo_core_config_t servo_config = test_servo_config();
    const motion_manager_config_t manager_config = {
        .remote_lease_timeout_us = 10000000u,
        .allowed_motion_sources = MOTION_SOURCE_MASK_NATIVE,
    };
    motion_request_t request = {
        .source = MOTION_SOURCE_NATIVE,
        .command_id = 30u,
        .kind = MOTION_COMMAND_ENABLE,
    };
    servo_core_t core;
    servo_core_output_t servo_output;
    motion_manager_t manager;
    motion_manager_status_t manager_status;
    motion_action_t action;
    float plant_position = 0.0f;
    float plant_velocity = 0.0f;
    uint32_t timestamp_us = 0u;
    unsigned int iteration;

    EXPECT_TRUE(servo_core_init(&core, &servo_config));
    EXPECT_TRUE(observe_servo(&core, 0.0f, 0.0f, timestamp_us) ==
                SERVO_CORE_STATUS_OK);
    EXPECT_TRUE(motion_manager_init(&manager, &manager_config, 0.0f));
    EXPECT_TRUE(motion_manager_submit(&manager,
                                      &request,
                                      timestamp_us,
                                      plant_position,
                                      &action) ==
                MOTION_SUBMIT_ACCEPTED);
    EXPECT_TRUE(apply_motion_action_to_servo(&core, &action));

    request.command_id = 31u;
    request.kind = MOTION_COMMAND_MOVE_ABSOLUTE;
    request.position_revolutions = 1.0f;
    EXPECT_TRUE(motion_manager_submit(&manager,
                                      &request,
                                      timestamp_us,
                                      plant_position,
                                      &action) ==
                MOTION_SUBMIT_ACCEPTED);
    EXPECT_TRUE(apply_motion_action_to_servo(&core, &action));

    for (iteration = 0u; iteration < 8000u; ++iteration)
    {
        bool motion_complete;
        float acceleration;

        timestamp_us += 1000u;
        EXPECT_TRUE(observe_servo(&core,
                                  plant_position,
                                  plant_velocity,
                                  timestamp_us) == SERVO_CORE_STATUS_OK);
        EXPECT_TRUE(servo_core_step(&core,
                                    timestamp_us,
                                    &servo_output) ==
                    SERVO_CORE_STATUS_OK);
        motion_complete =
            servo_output.trajectory_settled &&
            (fabsf(manager.target_position_revolutions -
                   servo_output.measured_position_revolutions) < 0.005f) &&
            (fabsf(servo_output.measured_velocity_revolutions_per_second) <
             0.02f);
        EXPECT_TRUE(motion_manager_poll(&manager,
                                        timestamp_us,
                                        motion_complete,
                                        false,
                                        &action));
        EXPECT_TRUE(apply_motion_action_to_servo(&core, &action));

        acceleration =
            (8.0f * servo_output.torque_current_request_amperes) -
            (2.0f * plant_velocity);
        plant_velocity += acceleration * 0.001f;
        plant_position += plant_velocity * 0.001f;
    }

    motion_manager_get_status(&manager, &manager_status);
    EXPECT_TRUE(manager_status.state == MOTION_STATE_READY);
    EXPECT_TRUE(manager_status.last_command_id == 31u);
    EXPECT_TRUE(manager_status.last_completion ==
                MOTION_COMPLETION_COMPLETED);
    EXPECT_TRUE(fabsf(plant_position - 1.0f) < 0.02f);
}

static void test_application_core_executes_remote_move(void)
{
    const application_core_config_t config = test_application_config(
        10000000u,
        MOTION_SOURCE_MASK_NATIVE);
    motion_request_t request = {
        .source = MOTION_SOURCE_NATIVE,
        .command_id = 40u,
        .kind = MOTION_COMMAND_ENABLE,
    };
    application_core_t application;
    application_core_output_t output;
    float plant_position = 0.0f;
    float plant_velocity = 0.0f;
    uint32_t timestamp_us = 0u;
    unsigned int iteration;

    EXPECT_TRUE(application_core_init(&application, &config));
    EXPECT_TRUE(observe_application(&application,
                                    0.0f,
                                    0.0f,
                                    timestamp_us) ==
                APPLICATION_CORE_STATUS_OK);
    EXPECT_TRUE(application_core_step(&application,
                                      timestamp_us,
                                      &output) ==
                APPLICATION_CORE_STATUS_OK);
    EXPECT_TRUE(!output.control_enabled);
    EXPECT_TRUE(output.torque_current_request_amperes == 0.0f);

    EXPECT_TRUE(application_core_submit_motion(&application,
                                                &request,
                                                timestamp_us) ==
                MOTION_SUBMIT_ACCEPTED);
    request.command_id = 41u;
    request.kind = MOTION_COMMAND_MOVE_ABSOLUTE;
    request.position_revolutions = 1.0f;
    EXPECT_TRUE(application_core_submit_motion(&application,
                                                &request,
                                                timestamp_us) ==
                MOTION_SUBMIT_ACCEPTED);

    for (iteration = 0u; iteration < 8000u; ++iteration)
    {
        float acceleration;

        timestamp_us += 1000u;
        EXPECT_TRUE(observe_application(&application,
                                        plant_position,
                                        plant_velocity,
                                        timestamp_us) ==
                    APPLICATION_CORE_STATUS_OK);
        EXPECT_TRUE(application_core_step(&application,
                                          timestamp_us,
                                          &output) ==
                    APPLICATION_CORE_STATUS_OK);
        EXPECT_TRUE(output.control_enabled);
        EXPECT_TRUE(fabsf(output.torque_current_request_amperes) <= 2.0f);

        acceleration =
            (8.0f * output.torque_current_request_amperes) -
            (2.0f * plant_velocity);
        plant_velocity += acceleration * 0.001f;
        plant_position += plant_velocity * 0.001f;
    }

    EXPECT_TRUE(output.motion.state == MOTION_STATE_READY);
    EXPECT_TRUE(output.motion.last_command_id == 41u);
    EXPECT_TRUE(output.motion.last_completion ==
                MOTION_COMPLETION_COMPLETED);
    EXPECT_TRUE(fabsf(plant_position - 1.0f) < 0.02f);
}

static void test_application_core_lease_stops_and_disables_plant(void)
{
    const application_core_config_t config = test_application_config(
        100000u,
        MOTION_SOURCE_MASK_NATIVE);
    motion_request_t request = {
        .source = MOTION_SOURCE_NATIVE,
        .command_id = 50u,
        .kind = MOTION_COMMAND_ENABLE,
    };
    application_core_t application;
    application_core_output_t output;
    float plant_position = 0.0f;
    float plant_velocity = 0.0f;
    uint32_t timestamp_us = 0u;
    unsigned int iteration;

    EXPECT_TRUE(application_core_init(&application, &config));
    EXPECT_TRUE(observe_application(&application,
                                    0.0f,
                                    0.0f,
                                    timestamp_us) ==
                APPLICATION_CORE_STATUS_OK);
    EXPECT_TRUE(application_core_submit_motion(&application,
                                                &request,
                                                timestamp_us) ==
                MOTION_SUBMIT_ACCEPTED);
    request.command_id = 51u;
    request.kind = MOTION_COMMAND_MOVE_ABSOLUTE;
    request.position_revolutions = 5.0f;
    EXPECT_TRUE(application_core_submit_motion(&application,
                                                &request,
                                                timestamp_us) ==
                MOTION_SUBMIT_ACCEPTED);

    for (iteration = 0u; iteration < 3000u; ++iteration)
    {
        float acceleration;

        timestamp_us += 1000u;
        EXPECT_TRUE(observe_application(&application,
                                        plant_position,
                                        plant_velocity,
                                        timestamp_us) ==
                    APPLICATION_CORE_STATUS_OK);
        EXPECT_TRUE(application_core_step(&application,
                                          timestamp_us,
                                          &output) ==
                    APPLICATION_CORE_STATUS_OK);
        acceleration =
            (8.0f * output.torque_current_request_amperes) -
            (2.0f * plant_velocity);
        plant_velocity += acceleration * 0.001f;
        plant_position += plant_velocity * 0.001f;
        if (output.motion.state == MOTION_STATE_DISABLED)
        {
            break;
        }
    }

    EXPECT_TRUE(iteration < 3000u);
    EXPECT_TRUE(output.motion.state == MOTION_STATE_DISABLED);
    EXPECT_TRUE(!output.control_enabled);
    EXPECT_TRUE(output.torque_current_request_amperes == 0.0f);
    EXPECT_TRUE(output.motion.last_command_id == 51u);
    EXPECT_TRUE(output.motion.last_completion ==
                MOTION_COMPLETION_ABORTED_LEASE);
    EXPECT_TRUE(plant_position < 0.25f);
}

static void test_application_core_maps_step_direction_stream(void)
{
    const application_core_config_t config = test_application_config(
        100000u,
        MOTION_SOURCE_MASK_STEP_DIRECTION);
    const motion_request_t remote_enable = {
        .source = MOTION_SOURCE_NATIVE,
        .command_id = 60u,
        .kind = MOTION_COMMAND_ENABLE,
    };
    application_core_t application;
    application_core_output_t output;
    motion_submit_status_t submit_status;

    EXPECT_TRUE(application_core_init(&application, &config));
    EXPECT_TRUE(observe_application(&application, 0.0f, 0.0f, 0u) ==
                APPLICATION_CORE_STATUS_OK);
    EXPECT_TRUE(application_core_submit_motion(&application,
                                                &remote_enable,
                                                0u) ==
                MOTION_SUBMIT_SOURCE_DISABLED);
    EXPECT_TRUE(application_core_update_step_direction(&application,
                                                       0,
                                                       false,
                                                       0u,
                                                       &submit_status));
    EXPECT_TRUE(observe_application(&application, 0.0f, 0.0f, 1000u) ==
                APPLICATION_CORE_STATUS_OK);
    EXPECT_TRUE(application_core_update_step_direction(&application,
                                                       0,
                                                       true,
                                                       1000u,
                                                       &submit_status));
    EXPECT_TRUE(submit_status == MOTION_SUBMIT_ACCEPTED);
    EXPECT_TRUE(observe_application(&application, 0.0f, 0.0f, 2000u) ==
                APPLICATION_CORE_STATUS_OK);
    EXPECT_TRUE(application_core_update_step_direction(&application,
                                                       160,
                                                       true,
                                                       2000u,
                                                       &submit_status));
    EXPECT_TRUE(submit_status == MOTION_SUBMIT_ACCEPTED);
    EXPECT_TRUE(application_core_step(&application, 2000u, &output) ==
                APPLICATION_CORE_STATUS_OK);
    EXPECT_TRUE(output.control_enabled);
    EXPECT_TRUE(output.motion.authority == MOTION_SOURCE_STEP_DIRECTION);
    EXPECT_TRUE(output.motion.state == MOTION_STATE_MOVING);
    EXPECT_TRUE(fabsf(output.motion.target_position_revolutions - 0.05f) <
                0.0001f);

    EXPECT_TRUE(observe_application(&application, 0.0f, 0.0f, 3000u) ==
                APPLICATION_CORE_STATUS_OK);
    EXPECT_TRUE(application_core_update_step_direction(&application,
                                                       160,
                                                       false,
                                                       3000u,
                                                       &submit_status));
    EXPECT_TRUE(submit_status == MOTION_SUBMIT_ACCEPTED);
    EXPECT_TRUE(application_core_step(&application, 3000u, &output) ==
                APPLICATION_CORE_STATUS_OK);
    EXPECT_TRUE(!output.control_enabled);
    EXPECT_TRUE(output.motion.state == MOTION_STATE_DISABLED);
    EXPECT_TRUE(output.torque_current_request_amperes == 0.0f);
}

static void test_application_core_fault_requires_safe_recovery(void)
{
    const application_core_config_t config = test_application_config(
        100000u,
        MOTION_SOURCE_MASK_NATIVE);
    const motion_request_t enable = {
        .source = MOTION_SOURCE_NATIVE,
        .command_id = 70u,
        .kind = MOTION_COMMAND_ENABLE,
    };
    application_core_t application;
    application_core_output_t output;

    EXPECT_TRUE(application_core_init(&application, &config));
    EXPECT_TRUE(observe_application(&application, 0.0f, 0.0f, 0u) ==
                APPLICATION_CORE_STATUS_OK);
    EXPECT_TRUE(application_core_submit_motion(&application,
                                                &enable,
                                                0u) ==
                MOTION_SUBMIT_ACCEPTED);
    EXPECT_TRUE(observe_application(&application,
                                    0.0f,
                                    21.0f,
                                    1000u) ==
                APPLICATION_CORE_STATUS_FAULTED);
    EXPECT_TRUE(application_core_step(&application, 1000u, &output) ==
                APPLICATION_CORE_STATUS_FAULTED);
    EXPECT_TRUE(!output.control_enabled);
    EXPECT_TRUE(output.motion.state == MOTION_STATE_FAULT);
    {
        const rotor_observation_t recovery_observation = {
            .position_revolutions = 0.0f,
            .velocity_revolutions_per_second = 0.0f,
            .timestamp_us = 2000u,
            .valid = true,
        };

        EXPECT_TRUE(!application_core_recover(
            &application, false, &recovery_observation));
        EXPECT_TRUE(application_core_recover(
            &application, true, &recovery_observation));
    }
    EXPECT_TRUE(application_core_step(&application, 2000u, &output) ==
                APPLICATION_CORE_STATUS_OK);
    EXPECT_TRUE(output.motion.state == MOTION_STATE_DISABLED);
}

static void test_application_core_invalid_step_stream_faults_immediately(void)
{
    const application_core_config_t config = test_application_config(
        100000u,
        MOTION_SOURCE_MASK_STEP_DIRECTION);
    application_core_t application;
    application_core_output_t output;
    motion_submit_status_t submit_status;

    EXPECT_TRUE(application_core_init(&application, &config));
    EXPECT_TRUE(observe_application(&application, 0.0f, 0.0f, 0u) ==
                APPLICATION_CORE_STATUS_OK);
    EXPECT_TRUE(application_core_update_step_direction(&application,
                                                       0,
                                                       true,
                                                       0u,
                                                       &submit_status));
    EXPECT_TRUE(submit_status == MOTION_SUBMIT_ACCEPTED);
    EXPECT_TRUE(application.control_enabled);
    EXPECT_TRUE(observe_application(&application, 0.0f, 0.0f, 1000u) ==
                APPLICATION_CORE_STATUS_OK);
    EXPECT_TRUE(!application_core_update_step_direction(&application,
                                                        1000,
                                                        true,
                                                        1000u,
                                                        &submit_status));
    EXPECT_TRUE(submit_status == MOTION_SUBMIT_FAULTED);
    EXPECT_TRUE(!application.control_enabled);
    EXPECT_TRUE(application.motion.state == MOTION_STATE_FAULT);
    EXPECT_TRUE(application_core_step(&application, 1000u, &output) ==
                APPLICATION_CORE_STATUS_FAULTED);
    EXPECT_TRUE(output.torque_current_request_amperes == 0.0f);
}

static phase_current_loop_config_t test_phase_current_loop_config(void)
{
    const phase_current_loop_config_t config = {
        .current_a_zero_raw = 2040u,
        .current_b_zero_raw = 2050u,
        .reference_limit_counts = 495u,
        .hard_current_limit_counts = 600u,
        .proportional_gain_q16_per_count =
            2 * (int32_t)PHASE_CURRENT_LOOP_Q16_ONE,
        .integral_gain_q16_per_count_per_step =
            (int32_t)PHASE_CURRENT_LOOP_Q16_ONE / 16,
        .phase_voltage_limit_permille = 200u,
        .duty_margin_permille = 100u,
        .current_a_polarity = 1,
        .current_b_polarity = -1,
    };

    return config;
}

static void test_phase_current_loop_generates_low_zero_bridge_duties(void)
{
    const phase_current_loop_config_t config =
        test_phase_current_loop_config();
    phase_current_loop_t loop;
    phase_current_loop_output_t output;

    EXPECT_TRUE(phase_current_loop_config_is_valid(&config));
    EXPECT_TRUE(phase_current_loop_init(&loop, &config));
    EXPECT_TRUE(phase_current_loop_set_reference_counts(&loop,
                                                        &config,
                                                        20,
                                                        -10));
    EXPECT_TRUE(phase_current_loop_start(&loop));
    EXPECT_TRUE(phase_current_loop_step(&loop,
                                        &config,
                                        config.current_a_zero_raw,
                                        config.current_b_zero_raw,
                                        &output));
    EXPECT_TRUE(output.phase_a_voltage_permille > 0);
    EXPECT_TRUE(output.phase_b_voltage_permille < 0);
    EXPECT_TRUE(output.duty_permille[0] == 0u);
    EXPECT_TRUE(output.duty_permille[1] ==
                (uint16_t)output.phase_a_voltage_permille);
    EXPECT_TRUE(output.duty_permille[2] == 0u);
    EXPECT_TRUE(output.duty_permille[3] ==
                (uint16_t)(-output.phase_b_voltage_permille));

    phase_current_loop_stop(&loop);
    EXPECT_TRUE(phase_current_loop_set_reference_counts(&loop,
                                                        &config,
                                                        -20,
                                                        10));
    EXPECT_TRUE(phase_current_loop_start(&loop));
    EXPECT_TRUE(phase_current_loop_step(&loop,
                                        &config,
                                        config.current_a_zero_raw,
                                        config.current_b_zero_raw,
                                        &output));
    EXPECT_TRUE(output.phase_a_voltage_permille < 0);
    EXPECT_TRUE(output.phase_b_voltage_permille > 0);
    EXPECT_TRUE(output.duty_permille[0] ==
                (uint16_t)(-output.phase_a_voltage_permille));
    EXPECT_TRUE(output.duty_permille[1] == 0u);
    EXPECT_TRUE(output.duty_permille[2] ==
                (uint16_t)output.phase_b_voltage_permille);
    EXPECT_TRUE(output.duty_permille[3] == 0u);
}

static void test_phase_current_loop_hard_limit_latches_both_polarities(void)
{
    const phase_current_loop_config_t config =
        test_phase_current_loop_config();
    phase_current_loop_t loop;
    phase_current_loop_output_t output;

    EXPECT_TRUE(phase_current_loop_init(&loop, &config));
    EXPECT_TRUE(phase_current_loop_start(&loop));
    EXPECT_TRUE(!phase_current_loop_step(
        &loop,
        &config,
        (uint16_t)(config.current_a_zero_raw +
                   config.hard_current_limit_counts + 1u),
        (uint16_t)(config.current_b_zero_raw -
                   config.hard_current_limit_counts - 1u),
        &output));
    EXPECT_TRUE((loop.fault_flags &
                 PHASE_CURRENT_LOOP_FAULT_OVERCURRENT_A) != 0u);
    EXPECT_TRUE((loop.fault_flags &
                 PHASE_CURRENT_LOOP_FAULT_OVERCURRENT_B) != 0u);
    EXPECT_TRUE(!loop.running);
    EXPECT_TRUE(output.current_a_measured_counts == 601);
    EXPECT_TRUE(output.current_b_measured_counts == 601);
    EXPECT_TRUE(output.duty_permille[0] == 0u);
    EXPECT_TRUE(!phase_current_loop_start(&loop));
}

static void test_phase_current_loop_rejects_excess_reference(void)
{
    const phase_current_loop_config_t config =
        test_phase_current_loop_config();
    phase_current_loop_t loop;

    EXPECT_TRUE(phase_current_loop_init(&loop, &config));
    EXPECT_TRUE(!phase_current_loop_set_reference_counts(
        &loop,
        &config,
        (int16_t)(config.reference_limit_counts + 1u),
        0));
    EXPECT_TRUE((loop.fault_flags &
                 PHASE_CURRENT_LOOP_FAULT_INVALID_REFERENCE) != 0u);
    EXPECT_TRUE(!phase_current_loop_start(&loop));
}

static void test_phase_current_loop_anti_windup_recovers_from_saturation(void)
{
    phase_current_loop_config_t config = test_phase_current_loop_config();
    phase_current_loop_t loop;
    phase_current_loop_output_t output;
    unsigned int step;

    config.proportional_gain_q16_per_count =
        20 * (int32_t)PHASE_CURRENT_LOOP_Q16_ONE;
    EXPECT_TRUE(phase_current_loop_init(&loop, &config));
    EXPECT_TRUE(phase_current_loop_set_reference_counts(&loop,
                                                        &config,
                                                        50,
                                                        0));
    EXPECT_TRUE(phase_current_loop_start(&loop));
    for (step = 0u; step < 1000u; ++step)
    {
        EXPECT_TRUE(phase_current_loop_step(&loop,
                                            &config,
                                            config.current_a_zero_raw,
                                            config.current_b_zero_raw,
                                            &output));
        EXPECT_TRUE(output.phase_a_voltage_permille == 200);
    }
    EXPECT_TRUE(loop.current_a_integrator_q16 == 0);

    EXPECT_TRUE(phase_current_loop_set_reference_counts(&loop,
                                                        &config,
                                                        0,
                                                        0));
    EXPECT_TRUE(phase_current_loop_step(&loop,
                                        &config,
                                        config.current_a_zero_raw,
                                        config.current_b_zero_raw,
                                        &output));
    EXPECT_TRUE(output.phase_a_voltage_permille == 0);
    EXPECT_TRUE(output.duty_permille[0] == 0u);
    EXPECT_TRUE(output.duty_permille[1] == 0u);
    EXPECT_TRUE(output.duty_permille[2] == 0u);
    EXPECT_TRUE(output.duty_permille[3] == 0u);
}

static void test_rotating_current_test_generates_quadrature_references(void)
{
    rotating_current_test_t generator = {0};
    int16_t current_a;
    int16_t current_b;

    EXPECT_TRUE(rotating_current_test_init(&generator,
                                           40,
                                           0x40000000u,
                                           0u));
    EXPECT_TRUE(rotating_current_test_step(&generator,
                                           &current_a,
                                           &current_b));
    EXPECT_TRUE(current_a == 40);
    EXPECT_TRUE(current_b == 0);
    EXPECT_TRUE(rotating_current_test_step(&generator,
                                           &current_a,
                                           &current_b));
    EXPECT_TRUE(current_a == 0);
    EXPECT_TRUE(current_b == 40);
    EXPECT_TRUE(rotating_current_test_step(&generator,
                                           &current_a,
                                           &current_b));
    EXPECT_TRUE(current_a == -40);
    EXPECT_TRUE(current_b == 0);
}

static aligned_torque_config_t test_aligned_torque_config(void)
{
    const aligned_torque_config_t config = {
        .maximum_current_counts = 248u,
        .maximum_current_slew_counts_per_second = 1000u,
        .maximum_velocity_revolutions_per_second_q16_16 = 1 << 16,
        .maximum_acceleration_revolutions_per_second2_q16_16 = 20 << 16,
        .maximum_feedback_interval_us = 2000u,
        .minimum_duration_millis = 3u,
        .maximum_duration_millis = INT32_MAX,
    };

    return config;
}

static void test_phase_current_reference_maps_signed_quadrants(void)
{
    int16_t reference_a = 0;
    int16_t reference_b = 0;

    EXPECT_TRUE(phase_current_reference_from_polar(
        100, 0x00000000u, &reference_a, &reference_b));
    EXPECT_TRUE(reference_a == 100);
    EXPECT_TRUE(reference_b == 0);
    EXPECT_TRUE(phase_current_reference_from_polar(
        100, 0x40000000u, &reference_a, &reference_b));
    EXPECT_TRUE(reference_a == 0);
    EXPECT_TRUE(reference_b == 100);
    EXPECT_TRUE(phase_current_reference_from_polar(
        -100, 0x80000000u, &reference_a, &reference_b));
    EXPECT_TRUE(reference_a == 100);
    EXPECT_TRUE(reference_b == 0);
    EXPECT_TRUE(phase_current_reference_from_polar(
        100, 0x20000000u, &reference_a, &reference_b));
    EXPECT_TRUE((reference_a >= 70) && (reference_a <= 71));
    EXPECT_TRUE((reference_b >= 70) && (reference_b <= 71));
    EXPECT_TRUE(!phase_current_reference_from_polar(
        100, 0u, NULL, &reference_b));
    EXPECT_TRUE(phase_current_reference_from_polar(
        INT16_MIN, 0x80000000u, &reference_a, &reference_b));
    EXPECT_TRUE(reference_a == INT16_MAX);
    EXPECT_TRUE(reference_b == 0);
}

static void test_aligned_torque_ramps_signed_q_current_and_deadlines(void)
{
    const aligned_torque_config_t config = test_aligned_torque_config();
    aligned_torque_controller_t controller;
    aligned_torque_status_t status;
    uint32_t step;

    EXPECT_TRUE(aligned_torque_config_is_valid(&config));
    EXPECT_TRUE(aligned_torque_controller_init(&controller, &config));
    EXPECT_TRUE(aligned_torque_controller_start(
        &controller, 50, 5000u, 100u, 1000u, 0));
    for (step = 1u; step <= 50u; ++step)
    {
        EXPECT_TRUE(aligned_torque_controller_update(
            &controller,
            100u + step,
            1000u + step * 1000u,
            true,
            0u,
            0,
            true) == ALIGNED_TORQUE_EVENT_REFERENCE_CHANGED);
    }
    aligned_torque_controller_get_status(&controller, &status);
    EXPECT_TRUE(status.state == ALIGNED_TORQUE_STATE_HOLDING);
    EXPECT_TRUE(status.applied_q_current_counts == 50);
    EXPECT_TRUE(status.current_a_reference_counts == 0);
    EXPECT_TRUE(status.current_b_reference_counts == 50);

    EXPECT_TRUE(aligned_torque_controller_update(
        &controller, 151u, 52000u, true, 0x40000000u, 0, true) ==
        ALIGNED_TORQUE_EVENT_REFERENCE_CHANGED);
    aligned_torque_controller_get_status(&controller, &status);
    EXPECT_TRUE(status.current_a_reference_counts == -50);
    EXPECT_TRUE(status.current_b_reference_counts == 0);

    EXPECT_TRUE(aligned_torque_controller_update(
        &controller, 5100u, 53000u, true, 0u, 0, true) ==
        ALIGNED_TORQUE_EVENT_COMPLETED);
    aligned_torque_controller_get_status(&controller, &status);
    EXPECT_TRUE(status.state == ALIGNED_TORQUE_STATE_COMPLETE);
    EXPECT_TRUE(status.result == ALIGNED_TORQUE_RESULT_DEADLINE);
    EXPECT_TRUE(status.applied_q_current_counts == 0);
    EXPECT_TRUE(status.current_a_reference_counts == 0);
    EXPECT_TRUE(status.current_b_reference_counts == 0);
    EXPECT_TRUE(!aligned_torque_controller_is_active(&controller));

    EXPECT_TRUE(aligned_torque_controller_start(
        &controller, -50, 100u, 400u, 60000u, 0));
    EXPECT_TRUE(aligned_torque_controller_update(
        &controller, 401u, 61000u, true, 0u, 0, true) ==
        ALIGNED_TORQUE_EVENT_REFERENCE_CHANGED);
    aligned_torque_controller_get_status(&controller, &status);
    EXPECT_TRUE(status.applied_q_current_counts == -1);
    EXPECT_TRUE(status.current_a_reference_counts == 0);
    EXPECT_TRUE(status.current_b_reference_counts == -1);
    EXPECT_TRUE(aligned_torque_controller_stop(&controller, 402u));
    aligned_torque_controller_get_status(&controller, &status);
    EXPECT_TRUE(status.state == ALIGNED_TORQUE_STATE_STOPPED);
    EXPECT_TRUE(status.result == ALIGNED_TORQUE_RESULT_STOPPED);
}

static void test_aligned_torque_rejects_unsafe_feedback_and_backend(void)
{
    const aligned_torque_config_t config = test_aligned_torque_config();
    aligned_torque_config_t invalid_config = config;
    aligned_torque_controller_t controller;
    aligned_torque_status_t status;

    invalid_config.maximum_feedback_interval_us = 0u;
    EXPECT_TRUE(!aligned_torque_config_is_valid(&invalid_config));
    invalid_config = config;
    invalid_config.maximum_current_counts = 0u;
    EXPECT_TRUE(!aligned_torque_config_is_valid(&invalid_config));
    invalid_config = config;
    invalid_config.maximum_duration_millis = UINT32_C(0x80000000);
    EXPECT_TRUE(!aligned_torque_config_is_valid(&invalid_config));
    EXPECT_TRUE(aligned_torque_controller_init(&controller, &config));
    EXPECT_TRUE(!aligned_torque_controller_start(
        &controller, 0, 200u, 0u, 0u, 0));
    EXPECT_TRUE(!aligned_torque_controller_start(
        &controller, 249, 200u, 0u, 0u, 0));
    EXPECT_TRUE(!aligned_torque_controller_start(
        &controller, -249, 200u, 0u, 0u, 0));
    EXPECT_TRUE(!aligned_torque_controller_start(
        &controller, 50, 2u, 0u, 0u, 0));
    EXPECT_TRUE(!aligned_torque_controller_start(
        &controller, 50, UINT32_C(0x80000000), 0u, 0u, 0));
    EXPECT_TRUE(!aligned_torque_controller_start(
        &controller, 50, 200u, 0u, 0u, 65537));

    EXPECT_TRUE(aligned_torque_controller_start(
        &controller, 50, 200u, 0u, 1000u, 0));
    EXPECT_TRUE(aligned_torque_controller_update(
        &controller, 1u, 2000u, false, 0u, 0, true) ==
        ALIGNED_TORQUE_EVENT_FAILED);
    aligned_torque_controller_get_status(&controller, &status);
    EXPECT_TRUE(status.result == ALIGNED_TORQUE_RESULT_PHASE_INVALID);
    EXPECT_TRUE((status.fault_flags &
                 ALIGNED_TORQUE_FAULT_PHASE_INVALID) != 0u);

    EXPECT_TRUE(aligned_torque_controller_start(
        &controller, 50, 200u, 10u, 3000u, 0));
    EXPECT_TRUE(aligned_torque_controller_update(
        &controller, 11u, 6001u, true, 0u, 0, true) ==
        ALIGNED_TORQUE_EVENT_FAILED);
    aligned_torque_controller_get_status(&controller, &status);
    EXPECT_TRUE(status.result == ALIGNED_TORQUE_RESULT_FEEDBACK_TIMING);

    EXPECT_TRUE(aligned_torque_controller_start(
        &controller, 50, 200u, 20u, 7000u, 0));
    EXPECT_TRUE(aligned_torque_controller_update(
        &controller, 21u, 8000u, true, 0u, 65537, true) ==
        ALIGNED_TORQUE_EVENT_FAILED);
    aligned_torque_controller_get_status(&controller, &status);
    EXPECT_TRUE(status.result == ALIGNED_TORQUE_RESULT_OVERSPEED);

    EXPECT_TRUE(aligned_torque_controller_start(
        &controller, 50, 200u, 30u, 9000u, 0));
    EXPECT_TRUE(aligned_torque_controller_update(
        &controller, 31u, 10000u, true, 0u, 65536, true) ==
        ALIGNED_TORQUE_EVENT_FAILED);
    aligned_torque_controller_get_status(&controller, &status);
    EXPECT_TRUE(status.result == ALIGNED_TORQUE_RESULT_OVERACCELERATION);

    EXPECT_TRUE(aligned_torque_controller_start(
        &controller, 50, 200u, 40u, 11000u, 0));
    EXPECT_TRUE(aligned_torque_controller_update(
        &controller, 41u, 12000u, true, 0u, 0, false) ==
        ALIGNED_TORQUE_EVENT_FAILED);
    aligned_torque_controller_get_status(&controller, &status);
    EXPECT_TRUE(status.result == ALIGNED_TORQUE_RESULT_BACKEND_INACTIVE);

    EXPECT_TRUE(aligned_torque_controller_start(
        &controller, 50, 200u, 50u, 13000u, 0));
    EXPECT_TRUE(aligned_torque_controller_reference_rejected(
        &controller, 51u));
    aligned_torque_controller_get_status(&controller, &status);
    EXPECT_TRUE(status.result == ALIGNED_TORQUE_RESULT_REFERENCE_REJECTED);

    EXPECT_TRUE(aligned_torque_controller_start(
        &controller, 50, 100u, 100u, 14000u, 0));
    EXPECT_TRUE(aligned_torque_controller_update(
        &controller, 200u, 15000u, true, 0u, 0, false) ==
        ALIGNED_TORQUE_EVENT_FAILED);
    aligned_torque_controller_get_status(&controller, &status);
    EXPECT_TRUE(status.result == ALIGNED_TORQUE_RESULT_BACKEND_INACTIVE);
}

static void test_aligned_torque_requires_feedback_after_seed_sample(void)
{
    const aligned_torque_config_t config = test_aligned_torque_config();
    aligned_torque_controller_t controller;
    aligned_torque_status_t status;

    EXPECT_TRUE(aligned_torque_controller_init(&controller, &config));
    EXPECT_TRUE(aligned_torque_controller_start(
        &controller, 50, 200u, 10u, 5000u, 0));
    EXPECT_TRUE(aligned_torque_controller_update(
        &controller, 10u, 5000u, true, 0u, 0, true) ==
        ALIGNED_TORQUE_EVENT_FAILED);
    aligned_torque_controller_get_status(&controller, &status);
    EXPECT_TRUE(status.result == ALIGNED_TORQUE_RESULT_FEEDBACK_TIMING);

    EXPECT_TRUE(aligned_torque_controller_start(
        &controller, 50, 200u, 20u, 6000u, 0));
    EXPECT_TRUE(aligned_torque_controller_update(
        &controller, 21u, 7000u, true, 0u, 0, true) ==
        ALIGNED_TORQUE_EVENT_REFERENCE_CHANGED);
    aligned_torque_controller_get_status(&controller, &status);
    EXPECT_TRUE(status.state == ALIGNED_TORQUE_STATE_RAMPING);
    EXPECT_TRUE(status.applied_q_current_counts == 1);
}

static void test_aligned_torque_accepts_motor_rated_evaluation_envelope(void)
{
    const aligned_torque_config_t config = {
        .maximum_current_counts = 495u,
        .maximum_current_slew_counts_per_second = 10000u,
        .maximum_velocity_revolutions_per_second_q16_16 = 5 << 16,
        .maximum_acceleration_revolutions_per_second2_q16_16 = 1000 << 16,
        .maximum_feedback_interval_us = 2000u,
        .minimum_duration_millis = 3u,
        .maximum_duration_millis = INT32_MAX,
    };
    aligned_torque_controller_t controller;
    aligned_torque_status_t status;

    EXPECT_TRUE(aligned_torque_controller_init(&controller, &config));
    EXPECT_TRUE(aligned_torque_controller_start(
        &controller, 495, 5000u, 0u, 1000u, 4 << 16));
    EXPECT_TRUE(aligned_torque_controller_update(
        &controller, 1u, 2000u, true, 0u, 4 << 16, true) ==
        ALIGNED_TORQUE_EVENT_REFERENCE_CHANGED);
    aligned_torque_controller_get_status(&controller, &status);
    EXPECT_TRUE(status.applied_q_current_counts == 10);
    EXPECT_TRUE(status.current_a_reference_counts == 0);
    EXPECT_TRUE(status.current_b_reference_counts == 10);
    EXPECT_TRUE(aligned_torque_controller_stop(&controller, 2u));
    EXPECT_TRUE(!aligned_torque_controller_start(
        &controller, 496, 5000u, 3u, 3000u, 0));
    EXPECT_TRUE(!aligned_torque_controller_start(
        &controller, 495, 5000u, 3u, 3000u, (5 << 16) + 1));
}

static void test_aligned_torque_tracking_target_reuses_bounded_actuator(void)
{
    const aligned_torque_config_t config = test_aligned_torque_config();
    aligned_torque_controller_t controller;
    aligned_torque_status_t status;

    EXPECT_TRUE(aligned_torque_controller_init(&controller, &config));
    EXPECT_TRUE(aligned_torque_controller_start_tracking(
        &controller, 200u, 0u, 1000u, 0));
    EXPECT_TRUE(aligned_torque_controller_set_target(&controller, 50));
    EXPECT_TRUE(aligned_torque_controller_update(
        &controller, 1u, 2000u, true, 0u, 0, true) ==
        ALIGNED_TORQUE_EVENT_REFERENCE_CHANGED);
    aligned_torque_controller_get_status(&controller, &status);
    EXPECT_TRUE(status.requested_q_current_counts == 50);
    EXPECT_TRUE(status.applied_q_current_counts == 1);
    EXPECT_TRUE(aligned_torque_controller_set_target(&controller, -50));
    EXPECT_TRUE(aligned_torque_controller_update(
        &controller, 2u, 3000u, true, 0u, 0, true) ==
        ALIGNED_TORQUE_EVENT_REFERENCE_CHANGED);
    aligned_torque_controller_get_status(&controller, &status);
    EXPECT_TRUE(status.requested_q_current_counts == -50);
    EXPECT_TRUE(status.applied_q_current_counts == 0);
    EXPECT_TRUE(!aligned_torque_controller_set_target(&controller, 249));
    EXPECT_TRUE(aligned_torque_controller_stop(&controller, 3u));
    EXPECT_TRUE(!aligned_torque_controller_set_target(&controller, 1));
}

int main(void)
{
    test_reset_only_enters_diagnostic_after_passive_init();
    test_faults_converge_on_fault_state();
    test_fault_recovery_requires_explicit_safe_context();
    test_drive_supervisor_owns_diagnostic_and_motion_authority();
    test_readiness_loss_deauthorizes_or_faults();
    test_drive_supervisor_rejects_state_authority_mismatch();
    test_fault_latch_preserves_first_fault_and_accumulates_flags();
    test_watchdog_policy_services_only_on_schedule();
    test_watchdog_policy_latches_failed_health();
    test_watchdog_policy_rejects_foreground_deadline_miss();
    test_watchdog_policy_handles_millisecond_wrap();
    test_diagnostics_record_abi();
    test_dma_channel_budget_contract();
    test_dma_ring_copies_across_wrap();
    test_dma_ring_accounts_overwrite();
    test_dma_ring_handles_counter_wrap();
    test_mt6816_decodes_angle_and_even_parity();
    test_mt6816_reports_sensor_warning_flags();
    test_mt6816_rejects_bad_parity_without_publishing();
    test_mt6816_uses_one_coherent_burst();
    test_mt6816_preserves_transport_failure();
    test_boot_self_test_requires_every_gate();
    test_boot_self_test_failure_is_latched();
    test_interrupt_priority_contract();
    test_timebase_reconciles_preempted_systick_epoch();
    test_timebase_reconciliation_clamps_stale_samples();
    test_timebase_reconciliation_preserves_uint32_wrap();
    test_adc_channel_and_sample_order_contract();
    test_adc_sample_rejects_values_outside_12_bits();
    test_adc_calibration_uses_measured_front_end_scaling();
    test_adc_zero_calibration_and_milliamp_conversion();
    test_servo57d_oled_profile_is_valid();
    test_adc_display_labels_channels_and_rejects_invalid_values();
    test_adc_display_renders_both_signed_milliamp_values();
    test_encoder_display_renders_position_and_invalid_state();
    test_user_inputs_debounce_each_active_low_signal_independently();
    test_input_display_labels_five_raw_levels();
    test_pulse_input_display_labels_three_raw_levels();
    test_ssd1306_init_uses_one_bounded_command_transaction();
    test_ssd1306_frame_uses_configured_visible_window();
    test_ssd1306_partial_pages_use_requested_window();
    test_ssd1306_stops_after_transport_failure();
    test_native_protocol_crc_matches_standard_vector();
    test_native_protocol_codec_accepts_maximum_payload();
    test_command_service_rejects_invalid_identity_payload();
    test_native_protocol_ping_round_trip_handles_zero_bytes();
    test_native_protocol_reports_identity_and_capabilities();
    test_native_protocol_commissioning_console_round_trip();
    test_native_protocol_rejects_bad_crc_and_resynchronizes();
    test_native_protocol_discards_oversize_then_resynchronizes();
    test_native_protocol_rejects_bad_cobs_and_unknown_version();
    test_native_protocol_suppresses_foreign_broadcast_and_response();
    test_native_protocol_returns_bounded_command_errors();
    test_native_protocol_counts_transport_rejection();
    test_angle_tracker_unwraps_in_both_directions();
    test_angle_tracker_rejects_implausible_motion_without_advancing();
    test_encoder_liveness_requires_fresh_progress();
    test_encoder_liveness_handles_counter_and_timer_wrap();
    test_electrical_phase_predictor_advances_at_current_loop_rate();
    test_electrical_phase_predictor_handles_direction_age_and_wrap();
    test_motor_alignment_accepts_measured_stepper_geometry();
    test_configuration_store_persists_and_avoids_unchanged_writes();
    test_configuration_store_interrupted_update_keeps_old_slot();
    test_configuration_store_crc_fallback_and_persistent_clear();
    test_alignment_controller_commits_closed_sequence();
    test_alignment_controller_failure_preserves_calibration();
    test_alignment_controller_aborts_and_rejects_bad_feedback();
    test_alignment_controller_rejects_runtime_failures();
    test_motion_profile_respects_velocity_and_acceleration_limits();
    test_motion_profile_controlled_stop_decelerates_to_rest();
    test_pi_controller_prevents_integrator_windup();
    test_servo_core_latches_stale_rotor_feedback();
    test_servo_core_rejects_invalid_rotor_observations();
    test_servo_core_latches_following_error();
    test_servo_core_closes_position_loop_against_simple_plant();
    test_velocity_controller_tracks_bounded_simple_plant();
    test_velocity_controller_rejects_bounds_and_faults_feedback();
    test_velocity_controller_deadline_clears_current();
    test_velocity_controller_limits_current_and_recovers();
    test_velocity_controller_applies_alignment_direction();
    test_velocity_controller_accepts_dynamic_tracking_targets();
    test_position_controller_profiles_and_settles();
    test_position_controller_enforces_following_error_and_deadline();
    test_position_controller_preserves_velocity_correction_headroom();
    test_park_transform_round_trip();
    test_current_controller_limits_voltage_vector();
    test_current_controller_regulates_simple_rl_plant();
    test_phase_current_loop_generates_low_zero_bridge_duties();
    test_phase_current_loop_hard_limit_latches_both_polarities();
    test_phase_current_loop_rejects_excess_reference();
    test_phase_current_loop_anti_windup_recovers_from_saturation();
    test_rotating_current_test_generates_quadrature_references();
    test_phase_current_reference_maps_signed_quadrants();
    test_aligned_torque_ramps_signed_q_current_and_deadlines();
    test_aligned_torque_rejects_unsafe_feedback_and_backend();
    test_aligned_torque_requires_feedback_after_seed_sample();
    test_aligned_torque_accepts_motor_rated_evaluation_envelope();
    test_aligned_torque_tracking_target_reuses_bounded_actuator();
    test_motion_manager_enforces_authority_and_idempotency();
    test_motion_manager_lease_expiry_stops_then_disables();
    test_motion_manager_keepalive_is_explicit_and_retries_stay_safe();
    test_motion_manager_lease_deadline_survives_timestamp_wrap();
    test_motion_manager_allows_foreign_stop_and_latches_fault();
    test_step_direction_reanchors_and_tracks_signed_counts();
    test_step_direction_rejects_rate_and_handles_counter_wrap();
    test_motion_manager_reports_simulated_move_completion();
    test_application_core_executes_remote_move();
    test_application_core_lease_stops_and_disables_plant();
    test_application_core_maps_step_direction_stream();
    test_application_core_fault_requires_safe_recovery();
    test_application_core_invalid_step_stream_faults_immediately();

    if (s_failures != 0u)
    {
        printf("%u test assertion(s) failed\n", s_failures);
        return 1;
    }

    puts("all host unit tests passed");
    return 0;
}
