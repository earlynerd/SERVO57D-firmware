#include "mks57d/watchdog.h"

#include <stddef.h>

#include "n32l40x.h"

enum
{
    IWDG_WRITE_ACCESS_KEY = 0x5555u,
    IWDG_RELOAD_KEY = 0xAAAAu,
    IWDG_ENABLE_KEY = 0xCCCCu,
    IWDG_PRESCALER_DIV32 = 0x3u,
    IWDG_RELOAD_FOR_1S_AT_40KHZ = 1249u,
    IWDG_SYNC_TIMEOUT_ITERATIONS = 65536u
};

_Static_assert((IWDG_PRESCALER_DIV32 & ~IWDG_PREDIV_PD) == 0u,
               "IWDG prescaler does not fit the device field");
_Static_assert((IWDG_RELOAD_FOR_1S_AT_40KHZ & ~IWDG_RELV_REL) == 0u,
               "IWDG reload does not fit the device field");

static bool wait_for_register_sync(void)
{
    uint32_t remaining = IWDG_SYNC_TIMEOUT_ITERATIONS;

    while (((IWDG->STS & (IWDG_STS_PVU | IWDG_STS_CRVU)) != 0u) &&
           (remaining != 0u))
    {
        --remaining;
    }

    return (IWDG->STS & (IWDG_STS_PVU | IWDG_STS_CRVU)) == 0u;
}

watchdog_status_t watchdog_supervisor_start(watchdog_supervisor_t *supervisor,
                                             uint32_t now_millis)
{
    if (supervisor == NULL)
    {
        return WATCHDOG_STATUS_INVALID_ARGUMENT;
    }

    supervisor->started = false;
    watchdog_policy_init(&supervisor->policy, now_millis);

    RCC->CTRLSTS |= RCC_CTRLSTS_LSIEN;
    {
        uint32_t remaining = IWDG_SYNC_TIMEOUT_ITERATIONS;

        while (((RCC->CTRLSTS & RCC_CTRLSTS_LSIRD) == 0u) &&
               (remaining != 0u))
        {
            --remaining;
        }
        if ((RCC->CTRLSTS & RCC_CTRLSTS_LSIRD) == 0u)
        {
            return WATCHDOG_STATUS_LSI_TIMEOUT;
        }
    }

    IWDG->KEY = IWDG_WRITE_ACCESS_KEY;
    if (!wait_for_register_sync())
    {
        return WATCHDOG_STATUS_REGISTER_SYNC_TIMEOUT;
    }

    IWDG->PREDIV = IWDG_PRESCALER_DIV32;
    IWDG->RELV = IWDG_RELOAD_FOR_1S_AT_40KHZ;

    if (!wait_for_register_sync())
    {
        return WATCHDOG_STATUS_REGISTER_SYNC_TIMEOUT;
    }

    if (((IWDG->PREDIV & IWDG_PREDIV_PD) != IWDG_PRESCALER_DIV32) ||
        ((IWDG->RELV & IWDG_RELV_REL) != IWDG_RELOAD_FOR_1S_AT_40KHZ))
    {
        return WATCHDOG_STATUS_REGISTER_VERIFY_ERROR;
    }

    /*
     * The passive image has no bridge-control API, so halting the core cannot
     * preserve an energized output. This debug exemption must be removed before
     * a bridge-capable image exists.
     */
    DBG->CTRL |= DBG_CTRL_IWDG_STOP;

    IWDG->KEY = IWDG_RELOAD_KEY;
    IWDG->KEY = IWDG_ENABLE_KEY;
    __DSB();

    supervisor->started = true;
    return WATCHDOG_STATUS_READY;
}

watchdog_status_t watchdog_supervisor_poll(watchdog_supervisor_t *supervisor,
                                            uint32_t now_millis,
                                            bool liveness_checks_passed)
{
    watchdog_policy_action_t action;

    if (supervisor == NULL)
    {
        return WATCHDOG_STATUS_INVALID_ARGUMENT;
    }
    if (!supervisor->started)
    {
        return WATCHDOG_STATUS_NOT_STARTED;
    }

    action = watchdog_policy_step(&supervisor->policy,
                                  now_millis,
                                  liveness_checks_passed);
    if (action == WATCHDOG_POLICY_ACTION_FAIL)
    {
        return WATCHDOG_STATUS_LIVENESS_FAILED;
    }
    if (action == WATCHDOG_POLICY_ACTION_FEED)
    {
        IWDG->KEY = IWDG_RELOAD_KEY;
        __DSB();
    }

    return WATCHDOG_STATUS_READY;
}
