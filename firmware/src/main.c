#include <stdbool.h>
#include <stdint.h>

#include "mks57d/app_state.h"
#include "mks57d/board.h"
#include "mks57d/boot_self_test.h"
#include "mks57d/diagnostics.h"
#include "mks57d/interrupt_priority.h"
#include "mks57d/mt6816.h"
#include "mks57d/panic.h"
#include "mks57d/platform.h"
#include "mks57d/spi1.h"
#include "mks57d/timebase.h"
#include "mks57d/watchdog.h"
#include "n32l40x.h"

int main(void)
{
    enum
    {
        ENCODER_POWER_UP_DELAY_MS = 20u,
        ENCODER_SAMPLE_PERIOD_MS = 10u
    };
    app_state_t state = APP_STATE_RESET_SAFE;
    boot_self_test_t self_test;
    diagnostics_encoder_t encoder_diagnostics = {
        .status = MT6816_STATUS_NOT_ATTEMPTED,
        .transport_status = SPI_STATUS_NOT_READY,
    };
    spi_bus_t encoder_bus = {0};
    bool encoder_spi_ready = false;
    uint32_t heartbeat_count = 0u;
    uint32_t next_heartbeat;
    uint32_t next_encoder_sample;
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

    encoder_spi_ready = spi1_init(SystemCoreClock);
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

    if (!board_bridge_invariants_hold())
    {
        boot_self_test_fail(&self_test, BOOT_SELF_TEST_PASSIVE_BOARD);
        diagnostics_publish((uint32_t)state,
                            uptime_millis,
                            heartbeat_count,
                            (uint32_t)watchdog_status,
                            &self_test);
        platform_panic(PANIC_PASSIVE_BOARD_INVARIANT);
    }

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

    for (;;)
    {
        bool diagnostics_due = false;
        const uint32_t now = timebase_millis();

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
            diagnostics_publish((uint32_t)state,
                                now,
                                heartbeat_count,
                                (uint32_t)watchdog_status,
                                &self_test);
        }

        __WFI();
    }
}
