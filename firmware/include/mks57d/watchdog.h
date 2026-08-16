#ifndef MKS57D_WATCHDOG_H
#define MKS57D_WATCHDOG_H

#include <stdbool.h>
#include <stdint.h>

#include "mks57d/watchdog_policy.h"

typedef enum
{
    WATCHDOG_STATUS_READY = 0,
    WATCHDOG_STATUS_INVALID_ARGUMENT,
    WATCHDOG_STATUS_NOT_STARTED,
    WATCHDOG_STATUS_LSI_TIMEOUT,
    WATCHDOG_STATUS_REGISTER_SYNC_TIMEOUT,
    WATCHDOG_STATUS_REGISTER_VERIFY_ERROR,
    WATCHDOG_STATUS_LIVENESS_FAILED
} watchdog_status_t;

typedef struct
{
    watchdog_policy_t policy;
    bool started;
} watchdog_supervisor_t;

/*
 * The foreground supervisor is the sole owner of the IWDG reload operation.
 * Interrupt handlers and subsystem modules deliberately have no feed API.
 */
watchdog_status_t watchdog_supervisor_start(watchdog_supervisor_t *supervisor,
                                             uint32_t now_millis);
watchdog_status_t watchdog_supervisor_poll(watchdog_supervisor_t *supervisor,
                                            uint32_t now_millis,
                                            bool liveness_checks_passed);

#endif
