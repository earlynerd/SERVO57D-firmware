#include <stdbool.h>
#include <stdint.h>

#include "mks57d/app_state.h"
#include "mks57d/board.h"
#include "mks57d/diagnostics.h"
#include "mks57d/interrupt_priority.h"
#include "mks57d/panic.h"
#include "mks57d/platform.h"
#include "mks57d/timebase.h"
#include "mks57d/watchdog.h"
#include "n32l40x.h"

int main(void)
{
    app_state_t state = APP_STATE_RESET_SAFE;
    uint32_t heartbeat_count = 0u;
    uint32_t next_heartbeat;
    watchdog_supervisor_t watchdog;
    watchdog_status_t watchdog_status;

    if (!platform_early_memory_ready())
    {
        platform_panic(PANIC_EARLY_PLATFORM_INIT);
    }

    if (platform_clock_init() != PLATFORM_BOOT_READY)
    {
        platform_panic(PANIC_CLOCK_INIT);
    }

    if (!interrupt_priority_init())
    {
        platform_panic(PANIC_INTERRUPT_PRIORITY_INIT);
    }

    board_init_passive();

    if (!timebase_init())
    {
        platform_panic(PANIC_TIMEBASE_INIT);
    }

    state = app_state_transition(
        state,
        APP_EVENT_PASSIVE_INIT_COMPLETE,
        (app_transition_context_t){.safe_to_recover = false});

    if (state != APP_STATE_DIAGNOSTIC)
    {
        platform_panic(PANIC_INTERNAL_INVARIANT);
    }

    diagnostics_init((uint32_t)state,
                     timebase_millis(),
                     heartbeat_count,
                     (uint32_t)WATCHDOG_STATUS_NOT_STARTED);

    watchdog_status = watchdog_supervisor_start(&watchdog, timebase_millis());
    if (watchdog_status != WATCHDOG_STATUS_READY)
    {
        diagnostics_publish((uint32_t)state,
                            timebase_millis(),
                            heartbeat_count,
                            (uint32_t)watchdog_status);
        platform_panic(PANIC_WATCHDOG_INIT);
    }

    diagnostics_publish((uint32_t)state,
                        timebase_millis(),
                        heartbeat_count,
                        (uint32_t)watchdog_status);
    next_heartbeat = timebase_millis() + 250u;

    for (;;)
    {
        bool diagnostics_due = false;
        const uint32_t now = timebase_millis();
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
            state == APP_STATE_DIAGNOSTIC);
        if (watchdog_status != WATCHDOG_STATUS_READY)
        {
            diagnostics_publish((uint32_t)state,
                                now,
                                heartbeat_count,
                                (uint32_t)watchdog_status);
            platform_panic(PANIC_WATCHDOG_LIVENESS);
        }

        if (diagnostics_due)
        {
            diagnostics_publish((uint32_t)state,
                                now,
                                heartbeat_count,
                                (uint32_t)watchdog_status);
        }

        __WFI();
    }
}
