#ifndef MKS57D_WATCHDOG_POLICY_H
#define MKS57D_WATCHDOG_POLICY_H

#include <stdbool.h>
#include <stdint.h>

enum
{
    WATCHDOG_NOMINAL_TIMEOUT_MS = 1000u,
    WATCHDOG_SERVICE_INTERVAL_MS = 100u,
    WATCHDOG_FOREGROUND_DEADLINE_MS = 250u
};

typedef enum
{
    WATCHDOG_POLICY_ACTION_NONE = 0,
    WATCHDOG_POLICY_ACTION_FEED,
    WATCHDOG_POLICY_ACTION_FAIL
} watchdog_policy_action_t;

typedef struct
{
    uint32_t last_poll_millis;
    uint32_t next_service_millis;
    bool initialized;
    bool failed;
} watchdog_policy_t;

void watchdog_policy_init(watchdog_policy_t *policy, uint32_t now_millis);
watchdog_policy_action_t watchdog_policy_step(watchdog_policy_t *policy,
                                               uint32_t now_millis,
                                               bool liveness_checks_passed);

#endif
