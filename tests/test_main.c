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
#include "mks57d/application_core.h"
#include "mks57d/angle_tracker.h"
#include "mks57d/app_state.h"
#include "mks57d/boot_self_test.h"
#include "mks57d/bridge_characterizer.h"
#include "mks57d/bridge_display.h"
#include "mks57d/command_service.h"
#include "mks57d/current_controller.h"
#include "mks57d/diagnostics.h"
#include "mks57d/dma_channels.h"
#include "mks57d/dma_ring.h"
#include "mks57d/encoder_display.h"
#include "mks57d/fault_latch.h"
#include "mks57d/interrupt_priority.h"
#include "mks57d/input_display.h"
#include "mks57d/mt6816.h"
#include "mks57d/motion_manager.h"
#include "mks57d/native_protocol.h"
#include "mks57d/motion_profile.h"
#include "mks57d/pi_controller.h"
#include "mks57d/phase_current_loop.h"
#include "mks57d/rotating_current_test.h"
#include "mks57d/pulse_input_display.h"
#include "mks57d/servo_core.h"
#include "mks57d/ssd1306.h"
#include "mks57d/step_direction.h"
#include "mks57d/user_inputs.h"
#include "mks57d/watchdog_policy.h"

static unsigned int s_failures;

enum
{
    MOCK_I2C_MAX_CALLS = 32u,
    MOCK_I2C_MAX_BYTES = 32u,
    MOCK_SPI_MAX_BYTES = 8u,
    MOCK_PROTOCOL_CAPABILITIES = 0xA55Au
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
    command_commissioning_status_t status;
    command_encoder_status_t encoder_status;
    command_current_test_config_t requested_config;
    uint8_t requested_leg;
    uint32_t requested_duration_millis;
    size_t status_calls;
    size_t configure_calls;
    size_t start_calls;
    size_t stop_calls;
    size_t boot_status_calls;
    size_t encoder_status_calls;
} mock_commissioning_t;

static servo_core_config_t test_servo_config(void)
{
    const servo_core_config_t config = {
        .angle_tracker = {
            .counts_per_revolution = 16384u,
            .maximum_sample_interval_us = 2000u,
            .maximum_velocity_revolutions_per_second = 20.0f,
            .velocity_filter_alpha = 0.25f,
        },
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
        .encoder_stale_timeout_us = 3000u,
        .maximum_control_interval_us = 2000u,
        .position_gain_per_second = 8.0f,
        .maximum_following_error_revolutions = 0.5f,
        .maximum_current_amperes = 2.0f,
    };

    return config;
}

static application_core_config_t test_application_config(
    uint32_t lease_timeout_us,
    uint32_t allowed_motion_sources)
{
    const application_core_config_t config = {
        .servo = {
            .angle_tracker = {
                .counts_per_revolution = 16384u,
                .maximum_sample_interval_us = 2000u,
                .maximum_velocity_revolutions_per_second = 20.0f,
                .velocity_filter_alpha = 0.25f,
            },
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
            .encoder_stale_timeout_us = 3000u,
            .maximum_control_interval_us = 2000u,
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

static uint16_t simulated_encoder_raw(float position_revolutions)
{
    float wrapped = fmodf(position_revolutions, 1.0f);
    uint32_t raw;

    if (wrapped < 0.0f)
    {
        wrapped += 1.0f;
    }
    raw = (uint32_t)((wrapped * 16384.0f) + 0.5f);
    if (raw >= 16384u)
    {
        raw = 0u;
    }
    return (uint16_t)raw;
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
    const app_transition_context_t unsafe = {.safe_to_recover = false};

    EXPECT_TRUE(app_state_transition(APP_STATE_RESET_SAFE,
                                     APP_EVENT_FAULT_ACKNOWLEDGED,
                                     unsafe) == APP_STATE_RESET_SAFE);
    EXPECT_TRUE(app_state_transition(APP_STATE_RESET_SAFE,
                                     APP_EVENT_PASSIVE_INIT_COMPLETE,
                                     unsafe) == APP_STATE_DIAGNOSTIC);
}

static void test_faults_converge_on_fault_state(void)
{
    const app_transition_context_t unsafe = {.safe_to_recover = false};

    EXPECT_TRUE(app_state_transition(APP_STATE_RESET_SAFE,
                                     APP_EVENT_FAULT_DETECTED,
                                     unsafe) == APP_STATE_FAULT);
    EXPECT_TRUE(app_state_transition(APP_STATE_RUN,
                                     APP_EVENT_FAULT_DETECTED,
                                     unsafe) == APP_STATE_FAULT);
}

static void test_fault_recovery_requires_explicit_safe_context(void)
{
    const app_transition_context_t unsafe = {.safe_to_recover = false};
    const app_transition_context_t safe = {.safe_to_recover = true};

    EXPECT_TRUE(app_state_transition(APP_STATE_FAULT,
                                     APP_EVENT_FAULT_ACKNOWLEDGED,
                                     unsafe) == APP_STATE_FAULT);
    EXPECT_TRUE(app_state_transition(APP_STATE_FAULT,
                                     APP_EVENT_FAULT_ACKNOWLEDGED,
                                     safe) == APP_STATE_DIAGNOSTIC);
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
                 DIAGNOSTICS_CAPABILITY_BRIDGE_CHARACTERIZER) != 0u);
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

static void test_adc_channel_and_sample_order_contract(void)
{
    adc_sample_t sample;
    volatile unsigned int current_b_channel = ADC1_CURRENT_B_CHANNEL;
    volatile unsigned int current_a_channel = ADC1_CURRENT_A_CHANNEL;
    volatile unsigned int vbus_channel = ADC1_VBUS_CHANNEL;
    volatile unsigned int max_clock_hz = ADC1_PASSIVE_MAX_CLOCK_HZ;

    EXPECT_TRUE(current_b_channel == 2u);
    EXPECT_TRUE(current_a_channel == 3u);
    EXPECT_TRUE(vbus_channel == 4u);
    EXPECT_TRUE(max_clock_hz == 2000000u);

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

static void test_servo57d_oled_candidate_profile_is_valid(void)
{
    const ssd1306_panel_config_t* config =
        &SSD1306_PANEL_SERVO57D_CANDIDATE;

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

    raw &= ~((uint32_t)USER_INPUT_KEY_ENTER);
    EXPECT_TRUE(!user_inputs_debouncer_update(&debouncer, raw));
    EXPECT_TRUE(!user_inputs_debouncer_update(&debouncer, raw));
    EXPECT_TRUE(user_inputs_debounced_levels(&debouncer) == USER_INPUT_MASK);
    EXPECT_TRUE(user_inputs_debouncer_update(&debouncer, raw));
    EXPECT_TRUE((user_inputs_debounced_levels(&debouncer) &
                 USER_INPUT_KEY_ENTER) == 0u);

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
                 USER_INPUT_KEY_MENU) != 0u);

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

static void test_bridge_characterizer_requires_release_and_stops_raw(void)
{
    bridge_characterizer_t characterizer = {0};
    uint32_t raw = USER_INPUT_MASK;
    uint32_t debounced = USER_INPUT_MASK;

    EXPECT_TRUE(!bridge_characterizer_init(NULL, raw, debounced));
    EXPECT_TRUE(!bridge_characterizer_init(
        &characterizer,
        raw | (1u << 12),
        debounced));
    EXPECT_TRUE(bridge_characterizer_init(&characterizer,
                                          raw,
                                          debounced));
    EXPECT_TRUE(characterizer.selected_leg == BRIDGE_CHARACTERIZER_LEG_A1);
    EXPECT_TRUE(!characterizer.active);

    raw &= ~((uint32_t)USER_INPUT_KEY_NEXT);
    debounced &= ~((uint32_t)USER_INPUT_KEY_NEXT);
    EXPECT_TRUE(bridge_characterizer_update(&characterizer,
                                             raw,
                                             debounced));
    EXPECT_TRUE(characterizer.selected_leg == BRIDGE_CHARACTERIZER_LEG_A2);
    EXPECT_TRUE(!bridge_characterizer_update(&characterizer,
                                              raw,
                                              debounced));
    EXPECT_TRUE(characterizer.selected_leg == BRIDGE_CHARACTERIZER_LEG_A2);

    raw |= USER_INPUT_KEY_NEXT;
    debounced |= USER_INPUT_KEY_NEXT;
    EXPECT_TRUE(!bridge_characterizer_update(&characterizer,
                                              raw,
                                              debounced));
    raw &= ~((uint32_t)USER_INPUT_KEY_ENTER);
    debounced &= ~((uint32_t)USER_INPUT_KEY_ENTER);
    EXPECT_TRUE(bridge_characterizer_update(&characterizer,
                                             raw,
                                             debounced));
    EXPECT_TRUE(characterizer.active);

    EXPECT_TRUE(!bridge_characterizer_update(&characterizer,
                                              raw,
                                              debounced));
    EXPECT_TRUE(characterizer.active);

    raw |= USER_INPUT_KEY_ENTER;
    EXPECT_TRUE(bridge_characterizer_update(&characterizer,
                                             raw,
                                             debounced));
    EXPECT_TRUE(!characterizer.active);

    bridge_characterizer_stop(&characterizer);
    EXPECT_TRUE(!characterizer.active);
}

static void test_bridge_characterizer_does_not_start_held_at_boot(void)
{
    bridge_characterizer_t characterizer = {0};
    uint32_t raw = USER_INPUT_MASK & ~((uint32_t)USER_INPUT_KEY_ENTER);
    uint32_t debounced = raw;

    EXPECT_TRUE(bridge_characterizer_init(&characterizer,
                                          raw,
                                          debounced));
    EXPECT_TRUE(!bridge_characterizer_update(&characterizer,
                                              raw,
                                              debounced));
    EXPECT_TRUE(!characterizer.active);

    raw |= USER_INPUT_KEY_ENTER;
    debounced |= USER_INPUT_KEY_ENTER;
    EXPECT_TRUE(!bridge_characterizer_update(&characterizer,
                                              raw,
                                              debounced));
    raw &= ~((uint32_t)USER_INPUT_KEY_ENTER);
    debounced &= ~((uint32_t)USER_INPUT_KEY_ENTER);
    EXPECT_TRUE(bridge_characterizer_update(&characterizer,
                                             raw,
                                             debounced));
    EXPECT_TRUE(characterizer.active);

    raw &= ~((uint32_t)USER_INPUT_KEY_MENU);
    EXPECT_TRUE(bridge_characterizer_update(&characterizer,
                                             raw,
                                             debounced));
    EXPECT_TRUE(!characterizer.active);
}

static void test_bridge_display_labels_leg_and_zero_run_state(void)
{
    uint8_t a1_zero[BRIDGE_DISPLAY_FRAME_BYTES];
    uint8_t a1_run[BRIDGE_DISPLAY_FRAME_BYTES];
    uint8_t b2_zero[BRIDGE_DISPLAY_FRAME_BYTES];

    EXPECT_TRUE(!bridge_display_render(NULL,
                                       sizeof(a1_zero),
                                       BRIDGE_CHARACTERIZER_LEG_A1,
                                       false));
    EXPECT_TRUE(!bridge_display_render(a1_zero,
                                       sizeof(a1_zero) - 1u,
                                       BRIDGE_CHARACTERIZER_LEG_A1,
                                       false));
    EXPECT_TRUE(!bridge_display_render(a1_zero,
                                       sizeof(a1_zero),
                                       BRIDGE_CHARACTERIZER_LEG_COUNT,
                                       false));
    EXPECT_TRUE(bridge_display_render(a1_zero,
                                      sizeof(a1_zero),
                                      BRIDGE_CHARACTERIZER_LEG_A1,
                                      false));
    EXPECT_TRUE(bridge_display_render(a1_run,
                                      sizeof(a1_run),
                                      BRIDGE_CHARACTERIZER_LEG_A1,
                                      true));
    EXPECT_TRUE(bridge_display_render(b2_zero,
                                      sizeof(b2_zero),
                                      BRIDGE_CHARACTERIZER_LEG_B2,
                                      false));
    EXPECT_TRUE(memcmp(a1_zero, a1_run, sizeof(a1_zero)) != 0);
    EXPECT_TRUE(memcmp(a1_zero, b2_zero, sizeof(a1_zero)) != 0);
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
                    &SSD1306_PANEL_SERVO57D_CANDIDATE) == I2C_STATUS_OK);
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
                    &SSD1306_PANEL_SERVO57D_CANDIDATE,
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
                    &SSD1306_PANEL_SERVO57D_CANDIDATE,
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
                    &SSD1306_PANEL_SERVO57D_CANDIDATE,
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
                    &SSD1306_PANEL_SERVO57D_CANDIDATE,
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
    uint8_t wire[NATIVE_PROTOCOL_MAX_WIRE_FRAME_SIZE];
    native_protocol_server_t server;
    mock_protocol_tx_t transmit = {.accept = true};
    mock_commissioning_t commissioning = {
        .status = {
            .schema_version = 2u,
            .flags = 0x000007FFu,
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
        },
        .encoder_status = {
            .schema_version = 1u,
            .status = 1u,
            .transport_status = 0u,
            .angle_raw = 0x2345u,
            .flags = 0x02u,
            .sample_count = 0x01020304u,
            .error_count = 0x05060708u,
            .last_attempt_millis = 0x090A0B0Cu,
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
    EXPECT_TRUE(response.payload_length == 64u);
    EXPECT_TRUE(response.payload[0] == NATIVE_PROTOCOL_STATUS_OK);
    EXPECT_TRUE(response.payload[1] == 2u);
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
    EXPECT_TRUE(response.payload_length == 19u);
    EXPECT_TRUE(response.payload[0] == NATIVE_PROTOCOL_STATUS_OK);
    EXPECT_TRUE(response.payload[1] == 1u);
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
    EXPECT_TRUE(angle_tracker_push(&tracker, 16380u, 0u));
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

static void test_servo_core_latches_stale_encoder_feedback(void)
{
    const servo_core_config_t config = test_servo_config();
    servo_core_t core;
    servo_core_output_t output;

    EXPECT_TRUE(servo_core_init(&core, &config));
    EXPECT_TRUE(servo_core_observe_encoder(&core, 0u, 0u) ==
                SERVO_CORE_STATUS_OK);
    EXPECT_TRUE(servo_core_step(&core, 1000u, &output) ==
                SERVO_CORE_STATUS_OK);
    EXPECT_TRUE(servo_core_step(&core, 2000u, &output) ==
                SERVO_CORE_STATUS_OK);
    EXPECT_TRUE(servo_core_step(&core, 4000u, &output) ==
                SERVO_CORE_STATUS_FAULTED);
    EXPECT_TRUE(!output.valid);
    EXPECT_TRUE(output.torque_current_request_amperes == 0.0f);
    EXPECT_TRUE((output.fault_flags & SERVO_FAULT_STALE_ENCODER) != 0u);
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
    EXPECT_TRUE(servo_core_observe_encoder(&core, 0u, timestamp_us) ==
                SERVO_CORE_STATUS_OK);
    EXPECT_TRUE(servo_core_set_position_target(&core, 1.0f) ==
                SERVO_CORE_STATUS_OK);

    for (iteration = 0u;
         (iteration < 500u) && !servo_core_is_faulted(&core);
         ++iteration)
    {
        timestamp_us += 1000u;
        EXPECT_TRUE(servo_core_observe_encoder(&core, 0u, timestamp_us) ==
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
    EXPECT_TRUE(servo_core_observe_encoder(&core, 0u, timestamp_us) ==
                SERVO_CORE_STATUS_OK);
    EXPECT_TRUE(servo_core_set_position_target(&core, 1.0f) ==
                SERVO_CORE_STATUS_OK);

    for (iteration = 0u; iteration < 8000u; ++iteration)
    {
        float acceleration;

        timestamp_us += 1000u;
        EXPECT_TRUE(servo_core_observe_encoder(
                        &core,
                        simulated_encoder_raw(plant_position),
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
    EXPECT_TRUE(servo_core_observe_encoder(&core, 0u, timestamp_us) ==
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
        EXPECT_TRUE(servo_core_observe_encoder(
                        &core,
                        simulated_encoder_raw(plant_position),
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
    EXPECT_TRUE(application_core_observe_encoder(&application,
                                                  0u,
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
        EXPECT_TRUE(application_core_observe_encoder(
                        &application,
                        simulated_encoder_raw(plant_position),
                        timestamp_us) == APPLICATION_CORE_STATUS_OK);
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
    EXPECT_TRUE(application_core_observe_encoder(&application,
                                                  0u,
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
        EXPECT_TRUE(application_core_observe_encoder(
                        &application,
                        simulated_encoder_raw(plant_position),
                        timestamp_us) == APPLICATION_CORE_STATUS_OK);
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
    EXPECT_TRUE(application_core_observe_encoder(&application, 0u, 0u) ==
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
    EXPECT_TRUE(application_core_observe_encoder(&application, 0u, 1000u) ==
                APPLICATION_CORE_STATUS_OK);
    EXPECT_TRUE(application_core_update_step_direction(&application,
                                                       0,
                                                       true,
                                                       1000u,
                                                       &submit_status));
    EXPECT_TRUE(submit_status == MOTION_SUBMIT_ACCEPTED);
    EXPECT_TRUE(application_core_observe_encoder(&application, 0u, 2000u) ==
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

    EXPECT_TRUE(application_core_observe_encoder(&application, 0u, 3000u) ==
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
    EXPECT_TRUE(application_core_observe_encoder(&application, 0u, 0u) ==
                APPLICATION_CORE_STATUS_OK);
    EXPECT_TRUE(application_core_submit_motion(&application,
                                                &enable,
                                                0u) ==
                MOTION_SUBMIT_ACCEPTED);
    EXPECT_TRUE(application_core_observe_encoder(&application,
                                                  20000u,
                                                  1000u) ==
                APPLICATION_CORE_STATUS_FAULTED);
    EXPECT_TRUE(application_core_step(&application, 1000u, &output) ==
                APPLICATION_CORE_STATUS_FAULTED);
    EXPECT_TRUE(!output.control_enabled);
    EXPECT_TRUE(output.motion.state == MOTION_STATE_FAULT);
    EXPECT_TRUE(!application_core_recover(&application,
                                          false,
                                          0u,
                                          2000u));
    EXPECT_TRUE(application_core_recover(&application,
                                         true,
                                         0u,
                                         2000u));
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
    EXPECT_TRUE(application_core_observe_encoder(&application, 0u, 0u) ==
                APPLICATION_CORE_STATUS_OK);
    EXPECT_TRUE(application_core_update_step_direction(&application,
                                                       0,
                                                       true,
                                                       0u,
                                                       &submit_status));
    EXPECT_TRUE(submit_status == MOTION_SUBMIT_ACCEPTED);
    EXPECT_TRUE(application.control_enabled);
    EXPECT_TRUE(application_core_observe_encoder(&application, 0u, 1000u) ==
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
        .reference_limit_counts = 50u,
        .hard_current_limit_counts = 100u,
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
    EXPECT_TRUE(output.current_a_measured_counts == 101);
    EXPECT_TRUE(output.current_b_measured_counts == 101);
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

int main(void)
{
    test_reset_only_enters_diagnostic_after_passive_init();
    test_faults_converge_on_fault_state();
    test_fault_recovery_requires_explicit_safe_context();
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
    test_adc_channel_and_sample_order_contract();
    test_adc_sample_rejects_values_outside_12_bits();
    test_adc_calibration_uses_measured_front_end_scaling();
    test_adc_zero_calibration_and_milliamp_conversion();
    test_servo57d_oled_candidate_profile_is_valid();
    test_adc_display_labels_channels_and_rejects_invalid_values();
    test_adc_display_renders_both_signed_milliamp_values();
    test_encoder_display_renders_position_and_invalid_state();
    test_user_inputs_debounce_each_active_low_signal_independently();
    test_input_display_labels_five_raw_levels();
    test_pulse_input_display_labels_three_raw_levels();
    test_bridge_characterizer_requires_release_and_stops_raw();
    test_bridge_characterizer_does_not_start_held_at_boot();
    test_bridge_display_labels_leg_and_zero_run_state();
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
    test_motion_profile_respects_velocity_and_acceleration_limits();
    test_motion_profile_controlled_stop_decelerates_to_rest();
    test_pi_controller_prevents_integrator_windup();
    test_servo_core_latches_stale_encoder_feedback();
    test_servo_core_latches_following_error();
    test_servo_core_closes_position_loop_against_simple_plant();
    test_park_transform_round_trip();
    test_current_controller_limits_voltage_vector();
    test_current_controller_regulates_simple_rl_plant();
    test_phase_current_loop_generates_low_zero_bridge_duties();
    test_phase_current_loop_hard_limit_latches_both_polarities();
    test_phase_current_loop_rejects_excess_reference();
    test_phase_current_loop_anti_windup_recovers_from_saturation();
    test_rotating_current_test_generates_quadrature_references();
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
