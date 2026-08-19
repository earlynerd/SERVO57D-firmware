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
#include "mks57d/diagnostics.h"
#include "mks57d/i2c1.h"
#include "mks57d/interrupt_priority.h"
#include "mks57d/mt6816.h"
#include "mks57d/native_protocol.h"
#include "mks57d/panic.h"
#include "mks57d/platform.h"
#include "mks57d/rs485.h"
#include "mks57d/spi1.h"
#include "mks57d/ssd1306.h"
#include "mks57d/timebase.h"
#include "mks57d/tim3_bridge_pwm.h"
#include "mks57d/user_inputs.h"
#include "mks57d/watchdog.h"
#include "n32l40x.h"

#if !defined(MKS57D_BRIDGE_CHARACTERIZATION_IMAGE)
#error "This entry point is reserved for the bridge characterization image"
#endif

_Static_assert((unsigned int)NATIVE_PROTOCOL_MAX_WIRE_FRAME_SIZE <=
                   (unsigned int)RS485_TX_MAX_FRAME_SIZE,
               "native frames must fit the bounded RS-485 TX staging buffer");
_Static_assert((unsigned int)ADC1_SYNCHRONOUS_CURRENT_FREQUENCY_HZ ==
                   (unsigned int)TIM3_BRIDGE_PWM_FREQUENCY_HZ,
               "ADC acquisition rate must match the PWM trigger rate");

enum
{
    DISPLAY_WIDTH = 72u,
    DISPLAY_HEIGHT = 40u,
    DISPLAY_FRAME_BYTES = DISPLAY_WIDTH * (DISPLAY_HEIGHT / 8u)
};

static uint8_t s_display_frame[DISPLAY_FRAME_BYTES];

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

int main(void)
{
    enum
    {
        ENCODER_POWER_UP_DELAY_MS = 20u,
        ENCODER_SAMPLE_PERIOD_MS = 10u,
        ADC_SNAPSHOT_PERIOD_MS = 10u,
        INPUT_SAMPLE_PERIOD_MS = 10u,
        DISPLAY_REFRESH_PERIOD_MS = 200u,
        RS485_FOREGROUND_DRAIN_BYTES = 64u
    };
    app_state_t state = APP_STATE_RESET_SAFE;
    boot_self_test_t self_test;
    diagnostics_encoder_t encoder_diagnostics = {
        .status = MT6816_STATUS_NOT_ATTEMPTED,
        .transport_status = SPI_STATUS_NOT_READY,
    };
    diagnostics_rs485_t rs485_diagnostics = {
        .status = RS485_STATUS_NOT_READY,
    };
    diagnostics_protocol_t protocol_diagnostics = {0};
    const command_service_context_t command_context = {
        .product_id = COMMAND_SERVICE_PRODUCT_ID_MKS57D,
        .firmware_major = MKS57D_FIRMWARE_VERSION_MAJOR,
        .firmware_minor = MKS57D_FIRMWARE_VERSION_MINOR,
        .firmware_patch = MKS57D_FIRMWARE_VERSION_PATCH,
        .protocol_major = NATIVE_PROTOCOL_VERSION_MAJOR,
        .protocol_minor = NATIVE_PROTOCOL_VERSION_MINOR,
        .capabilities = DIAGNOSTICS_CAPABILITIES_CURRENT,
    };
    native_protocol_server_t protocol_server;
    uint8_t rs485_receive_buffer[RS485_FOREGROUND_DRAIN_BYTES];
    adc1_current_snapshot_t adc_snapshot = {0};
    adc_zero_calibrator_t adc_zero_calibrator;
    adc_calibration_t adc_calibration = {0};
    i2c_bus_t display_bus = {0};
    spi_bus_t encoder_bus = {0};
    bool display_ready = false;
    bool adc_ready = false;
    bool adc_snapshot_valid = false;
    bool adc_calibration_ready = false;
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
    uint32_t next_display_refresh;
    uint32_t input_levels = USER_INPUT_MASK;
    uint32_t raw_input_levels = USER_INPUT_MASK;
    user_inputs_debouncer_t input_debouncer = {0};
    bridge_characterizer_t bridge_characterizer = {0};
    uint32_t uptime_millis = 0u;
    watchdog_supervisor_t watchdog;
    watchdog_status_t watchdog_status = WATCHDOG_STATUS_NOT_STARTED;

    if (!platform_early_memory_ready())
    {
        platform_panic(PANIC_EARLY_PLATFORM_INIT);
    }

    boot_self_test_init(&self_test, BOOT_SELF_TEST_REQUIRED_PASSIVE);
    boot_self_test_pass(&self_test, BOOT_SELF_TEST_EARLY_MEMORY);
    diagnostics_init((uint32_t)state,
                     uptime_millis,
                     heartbeat_count,
                     (uint32_t)watchdog_status,
                     &self_test);

    if (platform_clock_init() != PLATFORM_BOOT_READY)
    {
        boot_self_test_fail(&self_test, BOOT_SELF_TEST_CLOCK);
        diagnostics_publish((uint32_t)state,
                            uptime_millis,
                            heartbeat_count,
                            (uint32_t)watchdog_status,
                            &self_test);
        platform_panic(PANIC_CLOCK_INIT);
    }
    boot_self_test_pass(&self_test, BOOT_SELF_TEST_CLOCK);
    diagnostics_publish((uint32_t)state,
                        uptime_millis,
                        heartbeat_count,
                        (uint32_t)watchdog_status,
                        &self_test);

    if (!interrupt_priority_init())
    {
        boot_self_test_fail(&self_test, BOOT_SELF_TEST_INTERRUPT_POLICY);
        diagnostics_publish((uint32_t)state,
                            uptime_millis,
                            heartbeat_count,
                            (uint32_t)watchdog_status,
                            &self_test);
        platform_panic(PANIC_INTERRUPT_PRIORITY_INIT);
    }
    boot_self_test_pass(&self_test, BOOT_SELF_TEST_INTERRUPT_POLICY);
    diagnostics_publish((uint32_t)state,
                        uptime_millis,
                        heartbeat_count,
                        (uint32_t)watchdog_status,
                        &self_test);

    board_init_passive();
    if (!board_passive_invariants_hold())
    {
        boot_self_test_fail(&self_test, BOOT_SELF_TEST_PASSIVE_BOARD);
        diagnostics_publish((uint32_t)state,
                            uptime_millis,
                            heartbeat_count,
                            (uint32_t)watchdog_status,
                            &self_test);
        platform_panic(PANIC_PASSIVE_BOARD_INVARIANT);
    }
    boot_self_test_pass(&self_test, BOOT_SELF_TEST_PASSIVE_BOARD);
    diagnostics_publish((uint32_t)state,
                        uptime_millis,
                        heartbeat_count,
                        (uint32_t)watchdog_status,
                        &self_test);

    if (!timebase_init())
    {
        boot_self_test_fail(&self_test, BOOT_SELF_TEST_TIMEBASE);
        diagnostics_publish((uint32_t)state,
                            uptime_millis,
                            heartbeat_count,
                            (uint32_t)watchdog_status,
                            &self_test);
        platform_panic(PANIC_TIMEBASE_INIT);
    }
    boot_self_test_pass(&self_test, BOOT_SELF_TEST_TIMEBASE);
    uptime_millis = timebase_millis();
    diagnostics_publish((uint32_t)state,
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

    state = app_state_transition(
        state,
        APP_EVENT_PASSIVE_INIT_COMPLETE,
        (app_transition_context_t){.safe_to_recover = false});

    if (state != APP_STATE_DIAGNOSTIC)
    {
        boot_self_test_fail(&self_test, BOOT_SELF_TEST_APPLICATION_STATE);
        diagnostics_publish((uint32_t)state,
                            uptime_millis,
                            heartbeat_count,
                            (uint32_t)watchdog_status,
                            &self_test);
        platform_panic(PANIC_INTERNAL_INVARIANT);
    }
    boot_self_test_pass(&self_test, BOOT_SELF_TEST_APPLICATION_STATE);
    diagnostics_publish((uint32_t)state,
                        uptime_millis,
                        heartbeat_count,
                        (uint32_t)watchdog_status,
                        &self_test);

    watchdog_status = watchdog_supervisor_start(&watchdog, timebase_millis());
    if (watchdog_status != WATCHDOG_STATUS_READY)
    {
        boot_self_test_fail(&self_test, BOOT_SELF_TEST_WATCHDOG);
        diagnostics_publish((uint32_t)state,
                            timebase_millis(),
                            heartbeat_count,
                            (uint32_t)watchdog_status,
                            &self_test);
        platform_panic(PANIC_WATCHDOG_INIT);
    }
    boot_self_test_pass(&self_test, BOOT_SELF_TEST_WATCHDOG);

    diagnostics_publish((uint32_t)state,
                        timebase_millis(),
                        heartbeat_count,
                        (uint32_t)watchdog_status,
                        &self_test);
    next_heartbeat = timebase_millis() + 250u;
    next_encoder_sample = timebase_millis() + ENCODER_POWER_UP_DELAY_MS;
    next_adc_sample = timebase_millis();
    next_input_sample = timebase_millis();
    next_display_refresh = next_encoder_sample;

    for (;;)
    {
        bool diagnostics_due = false;
        const uint32_t now = timebase_millis();
        const bool bridge_was_active = bridge_characterizer.active;

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

        if (bridge_ready)
        {
            (void)bridge_characterizer_update(&bridge_characterizer,
                                              raw_input_levels,
                                              input_levels);
            if (bridge_was_active && !bridge_characterizer.active)
            {
                if (!board_bridge_characterizer_apply(
                        bridge_characterizer.selected_leg,
                        false))
                {
                    board_bridge_force_low_zero();
                    platform_panic(PANIC_BRIDGE_CHARACTERIZER_INIT);
                }
            }
            else if (bridge_characterizer.active && !bridge_was_active)
            {
                if (!adc_snapshot_valid)
                {
                    bridge_characterizer_stop(&bridge_characterizer);
                    if (!board_bridge_characterizer_apply(
                            bridge_characterizer.selected_leg,
                            false))
                    {
                        board_bridge_force_low_zero();
                        platform_panic(PANIC_BRIDGE_CHARACTERIZER_INIT);
                    }
                }
                else
                {
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

                    if (!board_bridge_characterizer_apply(
                            bridge_characterizer.selected_leg,
                            true))
                    {
                        bridge_characterizer_stop(&bridge_characterizer);
                        board_bridge_force_low_zero();
                        platform_panic(PANIC_BRIDGE_CHARACTERIZER_INIT);
                    }
                }
            }
        }

        if (rs485_ready && !bridge_characterizer.active)
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
                diagnostics_due = true;
            }
        }

        if (!bridge_characterizer.active && encoder_spi_ready &&
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
                if (!board_bridge_characterizer_apply(
                        bridge_characterizer.selected_leg,
                        false))
                {
                    board_bridge_force_low_zero();
                    platform_panic(PANIC_BRIDGE_CHARACTERIZER_INIT);
                }
            }
            next_adc_sample = now + ADC_SNAPSHOT_PERIOD_MS;
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
            (state == APP_STATE_DIAGNOSTIC) &&
                boot_self_test_ready(&self_test));
        if (watchdog_status != WATCHDOG_STATUS_READY)
        {
            diagnostics_publish((uint32_t)state,
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
            diagnostics_publish((uint32_t)state,
                                now,
                                heartbeat_count,
                                (uint32_t)watchdog_status,
                                &self_test);
        }

        if (display_ready &&
            ((int32_t)(now - next_display_refresh) >= 0))
        {
            bool rendered;

            if (!adc_ready)
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
