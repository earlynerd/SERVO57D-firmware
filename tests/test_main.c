#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "mks57d/adc1.h"
#include "mks57d/app_state.h"
#include "mks57d/boot_self_test.h"
#include "mks57d/diagnostics.h"
#include "mks57d/fault_latch.h"
#include "mks57d/interrupt_priority.h"
#include "mks57d/mt6816.h"
#include "mks57d/ssd1306.h"
#include "mks57d/watchdog_policy.h"

static unsigned int s_failures;

enum
{
    MOCK_I2C_MAX_CALLS = 32u,
    MOCK_I2C_MAX_BYTES = 32u,
    MOCK_SPI_MAX_BYTES = 8u
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

    EXPECT_TRUE(magic == 0x4D4B5335u);
    EXPECT_TRUE(schema == 2u);
    EXPECT_TRUE(record_size == 92u);
    EXPECT_TRUE(sequence_offset == 12u);
    EXPECT_TRUE(panic_offset == 48u);
    EXPECT_TRUE(encoder_offset == 64u);
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

    if (s_failures != 0u)
    {
        printf("%u test assertion(s) failed\n", s_failures);
        return 1;
    }

    puts("all host unit tests passed");
    return 0;
}
