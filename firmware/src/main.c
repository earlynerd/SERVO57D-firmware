#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "mks57d/adc1.h"
#include "mks57d/adc_calibration.h"
#include "mks57d/adc_display.h"
#include "mks57d/app_state.h"
#include "mks57d/board.h"
#include "mks57d/board_inputs.h"
#include "mks57d/boot_self_test.h"
#include "mks57d/bridge_characterizer.h"
#include "mks57d/bridge_display.h"
#include "mks57d/command_service.h"
#include "mks57d/current_loop_backend.h"
#include "mks57d/diagnostics.h"
#include "mks57d/i2c1.h"
#include "mks57d/interrupt_priority.h"
#include "mks57d/mt6816.h"
#include "mks57d/native_protocol.h"
#include "mks57d/panic.h"
#include "mks57d/platform.h"
#include "mks57d/rs485.h"
#include "mks57d/rotating_current_test.h"
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

enum
{
    COMMISSIONING_STATUS_SCHEMA_VERSION = 2u,
    ENCODER_STATUS_SCHEMA_VERSION = 1u,
    CURRENT_TRACE_SCHEMA_VERSION = 1u,
    CURRENT_TEST_MINIMUM_FREQUENCY_MILLIHZ = 1u,
    CURRENT_TEST_MAXIMUM_FREQUENCY_MILLIHZ = 50000u,
    CURRENT_TEST_MINIMUM_REMOTE_DURATION_MS = 100u,
    CURRENT_TEST_MAXIMUM_REMOTE_DURATION_MS = 60000u
};

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
    bridge_characterizer_t* bridge_characterizer;
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
} commissioning_command_context_t;

static bool encoder_control_ready(
    const diagnostics_encoder_t* encoder_diagnostics)
{
    return (encoder_diagnostics != NULL) &&
           (encoder_diagnostics->status == (uint32_t)MT6816_STATUS_OK) &&
           (encoder_diagnostics->transport_status ==
            (uint32_t)SPI_STATUS_OK) &&
           (encoder_diagnostics->flags == 0u) &&
           (encoder_diagnostics->sample_count != 0u);
}

static uint32_t current_test_initial_phase(
    bridge_characterizer_leg_t selected_leg)
{
    switch (selected_leg)
    {
        case BRIDGE_CHARACTERIZER_LEG_A1:
            return 0x80000000u;
        case BRIDGE_CHARACTERIZER_LEG_B1:
            return 0x40000000u;
        case BRIDGE_CHARACTERIZER_LEG_A2:
            return 0x00000000u;
        case BRIDGE_CHARACTERIZER_LEG_B2:
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
    commissioning_command_context_t* commissioning = context;
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
    status->selected_leg =
        (uint8_t)commissioning->bridge_characterizer->selected_leg;
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
    commissioning_command_context_t* commissioning = context;

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
        commissioning->bridge_characterizer->active ||
        commissioning->remote_start_requested)
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
    commissioning_command_context_t* commissioning = context;
    current_loop_backend_snapshot_t loop = {0};

    if (commissioning == NULL)
    {
        return COMMAND_STATUS_INTERNAL_ERROR;
    }
    if ((selected_leg >= (uint8_t)BRIDGE_CHARACTERIZER_LEG_COUNT) ||
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
        commissioning->bridge_characterizer->active ||
        commissioning->remote_start_requested ||
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
    commissioning_command_context_t* commissioning = context;

    if (commissioning == NULL)
    {
        return COMMAND_STATUS_INTERNAL_ERROR;
    }

    commissioning->remote_start_requested = false;
    commissioning->remote_stop_requested = true;
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
    commissioning_command_context_t* commissioning = context;
    const diagnostics_encoder_t* encoder;

    if ((commissioning == NULL) || (status == NULL) ||
        (commissioning->encoder_diagnostics == NULL))
    {
        return COMMAND_STATUS_INTERNAL_ERROR;
    }

    encoder = commissioning->encoder_diagnostics;
    status->schema_version = ENCODER_STATUS_SCHEMA_VERSION;
    status->status = (uint8_t)encoder->status;
    status->transport_status = (uint8_t)encoder->transport_status;
    status->angle_raw = (uint16_t)encoder->angle_raw;
    status->flags = (uint8_t)encoder->flags;
    status->sample_count = encoder->sample_count;
    status->error_count = encoder->error_count;
    status->last_attempt_millis = encoder->last_attempt_millis;
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

static bool display_bringup_attempt(i2c_bus_t* bus)
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
            &SSD1306_PANEL_SERVO57D_CANDIDATE) != I2C_STATUS_OK)
    {
        return false;
    }

    return ssd1306_write_frame(
               bus,
               &SSD1306_PANEL_SERVO57D_CANDIDATE,
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
        ENCODER_SAMPLE_PERIOD_MS = 10u,
        ADC_SNAPSHOT_PERIOD_MS = 10u,
        INPUT_SAMPLE_PERIOD_MS = 10u,
        CURRENT_TEST_REFERENCE_PERIOD_MS = 1u,
        DISPLAY_REFRESH_PERIOD_MS = 200u,
        RS485_FOREGROUND_DRAIN_BYTES = 64u,
        CURRENT_LOOP_REFERENCE_LIMIT_COUNTS = 165u,
        CURRENT_LOOP_HARD_LIMIT_COUNTS = 200u,
        CURRENT_LOOP_PHASE_VOLTAGE_LIMIT_PERMILLE = 700u,
        CURRENT_LOOP_DUTY_MARGIN_PERMILLE = 200u,
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
    app_supervisor_t drive_supervisor;
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
    spi_bus_t encoder_bus = {0};
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
    bool inputs_ready = false;
    bool rs485_ready = false;
    uint32_t heartbeat_count = 0u;
    uint32_t next_heartbeat;
    uint32_t next_encoder_sample;
    uint32_t next_adc_sample;
    uint32_t next_input_sample;
    uint32_t next_current_reference;
    uint32_t next_display_refresh;
    uint32_t input_levels = USER_INPUT_MASK;
    uint32_t raw_input_levels = USER_INPUT_MASK;
    user_inputs_debouncer_t input_debouncer = {0};
    bridge_characterizer_t bridge_characterizer = {0};
    uint32_t uptime_millis = 0u;
    watchdog_supervisor_t watchdog;
    watchdog_status_t watchdog_status = WATCHDOG_STATUS_NOT_STARTED;
    commissioning_command_context_t commissioning_context = {
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
        .bridge_characterizer = &bridge_characterizer,
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
    };

    if (!platform_early_memory_ready())
    {
        platform_panic(PANIC_EARLY_PLATFORM_INIT);
    }

    if (!app_supervisor_init(&drive_supervisor))
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

    display_ready = display_bringup_attempt(&display_bus);
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
    if (!inputs_ready ||
        !bridge_characterizer_init(&bridge_characterizer,
                                   raw_input_levels,
                                   input_levels))
    {
        platform_panic(PANIC_BRIDGE_CHARACTERIZER_INIT);
    }
    if (adc_ready)
    {
        adc_status = adc1_start_pwm_synchronized_current();
        adc_ready = adc_status == ADC1_STATUS_OK;
    }
    if (adc_ready &&
        !tim2_current_trigger_init(platform_apb1_timer_clock_hz()))
    {
        platform_panic(PANIC_BRIDGE_CHARACTERIZER_INIT);
    }
    if (!board_bridge_characterizer_init(
            platform_apb1_timer_clock_hz()))
    {
        platform_panic(PANIC_BRIDGE_CHARACTERIZER_INIT);
    }
    /* Current feedback is proven before bridge authority is restored. */
    bridge_ready = false;

    encoder_spi_ready = spi1_init(platform_apb2_clock_hz());
    if (encoder_spi_ready)
    {
        encoder_bus = spi1_bus();
    }
    else
    {
        encoder_diagnostics.status = MT6816_STATUS_TRANSPORT_ERROR;
        encoder_diagnostics.transport_status = SPI_STATUS_NOT_READY;
        encoder_diagnostics.error_count = 1u;
        encoder_diagnostics.last_attempt_millis = uptime_millis;
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
    next_encoder_sample = timebase_millis() + ENCODER_POWER_UP_DELAY_MS;
    next_adc_sample = timebase_millis();
    next_input_sample = timebase_millis();
    next_current_reference = timebase_millis();
    next_display_refresh = next_encoder_sample;

    for (;;)
    {
        bool diagnostics_due = false;
        const uint32_t now = timebase_millis();
        bool bridge_was_active;

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
                diagnostics_due = true;
            }
        }

        bridge_was_active = bridge_characterizer.active;
        if (bridge_ready)
        {
            if (commissioning_context.remote_authority_active &&
                (((raw_input_levels & USER_INPUT_KEY_MENU) == 0u) ||
                 ((int32_t)(now - commissioning_context.
                                      remote_run_deadline_millis) >= 0)))
            {
                commissioning_context.remote_stop_requested = true;
            }

            if (commissioning_context.remote_stop_requested)
            {
                bridge_characterizer_stop(&bridge_characterizer);
                bridge_characterizer.previous_debounced_levels =
                    input_levels;
                bridge_characterizer.enter_release_seen =
                    (raw_input_levels & USER_INPUT_KEY_ENTER) != 0u;
                commissioning_context.remote_authority_active = false;
                commissioning_context.remote_stop_requested = false;
            }
            else if (commissioning_context.remote_start_requested)
            {
                bridge_characterizer.selected_leg =
                    (bridge_characterizer_leg_t)
                        commissioning_context.remote_start_leg;
                bridge_characterizer.active = true;
                commissioning_context.remote_authority_active = true;
                commissioning_context.remote_run_deadline_millis =
                    now + commissioning_context.
                              remote_start_duration_millis;
                commissioning_context.remote_start_requested = false;
            }
            else if (!commissioning_context.remote_authority_active)
            {
                if (drive_supervisor.state == APP_STATE_READY)
                {
                    (void)bridge_characterizer_update(
                        &bridge_characterizer,
                        raw_input_levels,
                        input_levels);
                }
            }

            if (bridge_was_active && !bridge_characterizer.active)
            {
                if (!current_loop_backend_stop())
                {
                    board_bridge_force_low_zero();
                    platform_panic(PANIC_BRIDGE_CHARACTERIZER_INIT);
                }
                current_loop_backend_get_snapshot(&current_loop_snapshot);
                commissioning_context.remote_authority_active = false;
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
            else if (bridge_characterizer.active && !bridge_was_active)
            {
                const app_transition_context_t energize_context = {
                    .safe_to_energize =
                        bridge_ready && adc_snapshot_valid &&
                        encoder_control_ready(&encoder_diagnostics),
                };

                if (!adc_snapshot_valid ||
                    !app_supervisor_handle_event(
                        &drive_supervisor,
                        APP_EVENT_DIAGNOSTIC_OPERATION_REQUESTED,
                        energize_context))
                {
                    bridge_characterizer_stop(&bridge_characterizer);
                    commissioning_context.remote_authority_active = false;
                    if (!current_loop_backend_stop())
                    {
                        board_bridge_force_low_zero();
                        platform_panic(PANIC_BRIDGE_CHARACTERIZER_INIT);
                    }
                }
                else
                {
                    int16_t current_a_reference_counts;
                    int16_t current_b_reference_counts;

                    if (display_ready &&
                        (!bridge_display_render(
                             s_display_frame,
                             BRIDGE_DISPLAY_FRAME_BYTES,
                             bridge_characterizer.selected_leg,
                             true) ||
                         (ssd1306_write_pages(
                              &display_bus,
                              &SSD1306_PANEL_SERVO57D_CANDIDATE,
                              BRIDGE_DISPLAY_START_PAGE,
                              BRIDGE_DISPLAY_PAGE_COUNT,
                              s_display_frame,
                              BRIDGE_DISPLAY_FRAME_BYTES) != I2C_STATUS_OK)))
                    {
                        display_ready = false;
                    }

                    if (!rotating_current_test_init(
                            &current_test_generator,
                            (int16_t)commissioning_context.
                                test_amplitude_counts,
                            current_test_phase_increment(
                                commissioning_context.
                                    test_frequency_millihz),
                            current_test_initial_phase(
                                bridge_characterizer.selected_leg)) ||
                        !rotating_current_test_step(
                            &current_test_generator,
                            &current_a_reference_counts,
                            &current_b_reference_counts) ||
                        !current_loop_backend_set_reference_counts(
                            current_a_reference_counts,
                            current_b_reference_counts) ||
                        !current_loop_backend_start())
                    {
                        bridge_characterizer_stop(&bridge_characterizer);
                        commissioning_context.remote_authority_active = false;
                        (void)app_supervisor_handle_event(
                            &drive_supervisor,
                            APP_EVENT_FAULT_DETECTED,
                            (app_transition_context_t){0});
                        board_bridge_force_low_zero();
                        platform_panic(PANIC_BRIDGE_CHARACTERIZER_INIT);
                    }
                    next_current_reference =
                        now + CURRENT_TEST_REFERENCE_PERIOD_MS;
                    diagnostics_due = true;
                }
            }
        }

        if (bridge_characterizer.active &&
            !app_supervisor_bridge_authorized(&drive_supervisor))
        {
            bridge_characterizer_stop(&bridge_characterizer);
            commissioning_context.remote_authority_active = false;
            (void)app_supervisor_handle_event(
                &drive_supervisor,
                APP_EVENT_FAULT_DETECTED,
                (app_transition_context_t){0});
            board_bridge_force_low_zero();
            platform_panic(PANIC_INTERNAL_INVARIANT);
        }

        if (app_supervisor_bridge_authorized(&drive_supervisor) &&
            (drive_supervisor.authority == APP_AUTHORITY_DIAGNOSTIC) &&
            bridge_characterizer.active &&
            ((int32_t)(now - next_current_reference) >= 0))
        {
            int16_t current_a_reference_counts;
            int16_t current_b_reference_counts;

            if (!rotating_current_test_step(
                    &current_test_generator,
                    &current_a_reference_counts,
                    &current_b_reference_counts))
            {
                bridge_characterizer_stop(&bridge_characterizer);
                board_bridge_force_low_zero();
                platform_panic(PANIC_BRIDGE_CHARACTERIZER_INIT);
            }
            if (!current_loop_backend_set_reference_counts(
                    current_a_reference_counts,
                    current_b_reference_counts))
            {
                current_loop_backend_get_snapshot(&current_loop_snapshot);
                bridge_characterizer_stop(&bridge_characterizer);
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
                    platform_panic(PANIC_BRIDGE_CHARACTERIZER_INIT);
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

        if (encoder_spi_ready &&
            ((int32_t)(now - next_encoder_sample) >= 0))
        {
            mt6816_sample_t sample;
            spi_status_t transport_status = SPI_STATUS_OK;
            const mt6816_status_t encoder_status = mt6816_read_angle(
                &encoder_bus,
                &sample,
                &transport_status);

            encoder_diagnostics.status = (uint32_t)encoder_status;
            encoder_diagnostics.transport_status =
                (uint32_t)transport_status;
            encoder_diagnostics.last_attempt_millis = now;
            if (encoder_status == MT6816_STATUS_OK)
            {
                encoder_diagnostics.angle_raw = sample.angle_raw;
                encoder_diagnostics.flags = sample.flags;
                ++encoder_diagnostics.sample_count;
            }
            else
            {
                ++encoder_diagnostics.error_count;
            }
            diagnostics_publish_encoder(&encoder_diagnostics);
            next_encoder_sample = now + ENCODER_SAMPLE_PERIOD_MS;
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
                        platform_panic(PANIC_BRIDGE_CHARACTERIZER_INIT);
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
                bridge_characterizer_stop(&bridge_characterizer);
                commissioning_context.remote_authority_active = false;
                bridge_ready = false;
                commissioning_context.remote_start_requested = false;
                commissioning_context.remote_stop_requested = false;
                (void)app_supervisor_handle_event(
                    &drive_supervisor,
                    APP_EVENT_FAULT_DETECTED,
                    (app_transition_context_t){0});
                if (current_loop_initialized &&
                    !current_loop_backend_stop())
                {
                    board_bridge_force_low_zero();
                    platform_panic(PANIC_BRIDGE_CHARACTERIZER_INIT);
                }
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
                    bridge_characterizer_stop(&bridge_characterizer);
                    commissioning_context.remote_authority_active = false;
                    commissioning_context.remote_start_requested = false;
                    commissioning_context.remote_stop_requested = false;
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
                else if (bridge_characterizer.active &&
                         !current_loop_snapshot.active)
                {
                    bridge_characterizer_stop(&bridge_characterizer);
                    commissioning_context.remote_authority_active = false;
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
                encoder_control_ready(&encoder_diagnostics);

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
                if (app_supervisor_bridge_authorized(&drive_supervisor))
                {
                    bridge_characterizer_stop(&bridge_characterizer);
                    commissioning_context.remote_authority_active = false;
                    commissioning_context.remote_start_requested = false;
                    commissioning_context.remote_stop_requested = false;
                    if (!current_loop_backend_stop())
                    {
                        board_bridge_force_low_zero();
                        platform_panic(PANIC_BRIDGE_CHARACTERIZER_INIT);
                    }
                }
                if (!app_supervisor_handle_event(
                        &drive_supervisor,
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
                     &SSD1306_PANEL_SERVO57D_CANDIDATE,
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
