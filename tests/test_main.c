#include <stdbool.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "mks57d/adc1.h"
#include "mks57d/angle_tracker.h"
#include "mks57d/app_state.h"
#include "mks57d/boot_self_test.h"
#include "mks57d/command_service.h"
#include "mks57d/current_controller.h"
#include "mks57d/diagnostics.h"
#include "mks57d/dma_channels.h"
#include "mks57d/dma_ring.h"
#include "mks57d/fault_latch.h"
#include "mks57d/interrupt_priority.h"
#include "mks57d/mt6816.h"
#include "mks57d/native_protocol.h"
#include "mks57d/motion_profile.h"
#include "mks57d/pi_controller.h"
#include "mks57d/servo_core.h"
#include "mks57d/ssd1306.h"
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
    volatile uint32_t capabilities = DIAGNOSTICS_CAPABILITIES_CURRENT;

    EXPECT_TRUE(magic == 0x4D4B5335u);
    EXPECT_TRUE(schema == 4u);
    EXPECT_TRUE(record_size == 184u);
    EXPECT_TRUE(sequence_offset == 12u);
    EXPECT_TRUE(panic_offset == 48u);
    EXPECT_TRUE(encoder_offset == 64u);
    EXPECT_TRUE(rs485_offset == 92u);
    EXPECT_TRUE(protocol_offset == 136u);
    EXPECT_TRUE((capabilities &
                 DIAGNOSTICS_CAPABILITY_NATIVE_PROTOCOL) != 0u);
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
    EXPECT_TRUE(mock.call_count == 24u);
    EXPECT_TRUE(mock.lengths[0] == 7u);
    EXPECT_TRUE(mock.bytes[0][0] == 0x00u);
    EXPECT_TRUE(mock.bytes[0][1] == 0x21u);
    EXPECT_TRUE(mock.bytes[0][2] == 28u);
    EXPECT_TRUE(mock.bytes[0][3] == 99u);
    EXPECT_TRUE(mock.bytes[0][4] == 0x22u);
    EXPECT_TRUE(mock.bytes[0][5] == 0u);
    EXPECT_TRUE(mock.bytes[0][6] == 4u);
    EXPECT_TRUE(mock.bytes[1][0] == 0x40u);
    EXPECT_TRUE(mock.lengths[23] == 9u);
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
    test_servo57d_oled_candidate_profile_is_valid();
    test_ssd1306_init_uses_one_bounded_command_transaction();
    test_ssd1306_frame_uses_configured_visible_window();
    test_ssd1306_stops_after_transport_failure();
    test_native_protocol_crc_matches_standard_vector();
    test_native_protocol_codec_accepts_maximum_payload();
    test_command_service_rejects_invalid_identity_payload();
    test_native_protocol_ping_round_trip_handles_zero_bytes();
    test_native_protocol_reports_identity_and_capabilities();
    test_native_protocol_rejects_bad_crc_and_resynchronizes();
    test_native_protocol_discards_oversize_then_resynchronizes();
    test_native_protocol_rejects_bad_cobs_and_unknown_version();
    test_native_protocol_suppresses_foreign_broadcast_and_response();
    test_native_protocol_returns_bounded_command_errors();
    test_native_protocol_counts_transport_rejection();
    test_angle_tracker_unwraps_in_both_directions();
    test_angle_tracker_rejects_implausible_motion_without_advancing();
    test_motion_profile_respects_velocity_and_acceleration_limits();
    test_pi_controller_prevents_integrator_windup();
    test_servo_core_latches_stale_encoder_feedback();
    test_servo_core_latches_following_error();
    test_servo_core_closes_position_loop_against_simple_plant();
    test_park_transform_round_trip();
    test_current_controller_limits_voltage_vector();
    test_current_controller_regulates_simple_rl_plant();

    if (s_failures != 0u)
    {
        printf("%u test assertion(s) failed\n", s_failures);
        return 1;
    }

    puts("all host unit tests passed");
    return 0;
}
