#include <stdint.h>

#include "mks57d/app_state.h"
#include "mks57d/board.h"
#include "mks57d/panic.h"
#include "mks57d/platform.h"
#include "mks57d/timebase.h"
#include "n32l40x.h"

int main(void)
{
    app_state_t state = APP_STATE_RESET_SAFE;
    uint32_t next_heartbeat;

    if (!platform_early_memory_ready())
    {
        platform_panic(PANIC_EARLY_PLATFORM_INIT);
    }

    if (platform_clock_init() != PLATFORM_BOOT_READY)
    {
        platform_panic(PANIC_CLOCK_INIT);
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

    next_heartbeat = timebase_millis() + 250u;

    for (;;)
    {
        const uint32_t now = timebase_millis();
        if ((int32_t)(now - next_heartbeat) >= 0)
        {
            board_status_led_toggle();
            next_heartbeat += 250u;
        }

        __WFI();
    }
}
