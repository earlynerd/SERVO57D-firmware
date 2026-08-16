#include "mks57d/watchdog_policy.h"

#include <stddef.h>

_Static_assert(WATCHDOG_SERVICE_INTERVAL_MS < WATCHDOG_FOREGROUND_DEADLINE_MS,
               "watchdog service interval must precede the foreground deadline");
_Static_assert(WATCHDOG_FOREGROUND_DEADLINE_MS < WATCHDOG_NOMINAL_TIMEOUT_MS,
               "foreground deadline must precede the nominal hardware timeout");

void watchdog_policy_init(watchdog_policy_t *policy, uint32_t now_millis)
{
    if (policy == NULL)
    {
        return;
    }

    policy->last_poll_millis = now_millis;
    policy->next_service_millis = now_millis + WATCHDOG_SERVICE_INTERVAL_MS;
    policy->initialized = true;
    policy->failed = false;
}

watchdog_policy_action_t watchdog_policy_step(watchdog_policy_t *policy,
                                               uint32_t now_millis,
                                               bool liveness_checks_passed)
{
    uint32_t elapsed;

    if ((policy == NULL) || !policy->initialized || policy->failed)
    {
        return WATCHDOG_POLICY_ACTION_FAIL;
    }

    elapsed = now_millis - policy->last_poll_millis;
    if (!liveness_checks_passed ||
        (elapsed > WATCHDOG_FOREGROUND_DEADLINE_MS))
    {
        policy->failed = true;
        return WATCHDOG_POLICY_ACTION_FAIL;
    }

    policy->last_poll_millis = now_millis;

    if ((int32_t)(now_millis - policy->next_service_millis) >= 0)
    {
        /* Re-anchor instead of issuing catch-up feeds after foreground delay. */
        policy->next_service_millis = now_millis + WATCHDOG_SERVICE_INTERVAL_MS;
        return WATCHDOG_POLICY_ACTION_FEED;
    }

    return WATCHDOG_POLICY_ACTION_NONE;
}
