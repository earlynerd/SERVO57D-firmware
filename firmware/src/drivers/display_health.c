#include "mks57d/display_health.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

bool display_health_init(display_health_t* health,
                         bool ready,
                         uint32_t initial_error_status,
                         uint32_t now_millis,
                         uint32_t retry_period_millis)
{
    if ((health == NULL) || (retry_period_millis == 0u))
    {
        return false;
    }

    memset(health, 0, sizeof(*health));
    health->ready = ready;
    health->recovery_pending = !ready;
    health->retry_after_millis = now_millis + retry_period_millis;
    if (!ready)
    {
        health->error_count = 1u;
        health->last_error_status = initial_error_status;
        health->consecutive_errors = 1u;
    }
    return true;
}

bool display_health_is_ready(const display_health_t* health)
{
    return (health != NULL) && health->ready;
}

void display_health_record_write_success(display_health_t* health)
{
    if (health == NULL)
    {
        return;
    }

    health->consecutive_errors = 0u;
}

void display_health_record_write_failure(display_health_t* health,
                                         uint32_t error_status)
{
    if ((health == NULL) || !health->ready)
    {
        return;
    }

    ++health->error_count;
    health->last_error_status = error_status;
    if (health->consecutive_errors < UINT8_MAX)
    {
        ++health->consecutive_errors;
    }
}

bool display_health_recovery_due(const display_health_t* health,
                                 bool bridge_authorized,
                                 uint32_t now_millis)
{
    return (health != NULL) && !health->ready &&
           health->recovery_pending && !bridge_authorized &&
           ((int32_t)(now_millis - health->retry_after_millis) >= 0);
}

void display_health_record_recovery_result(
    display_health_t* health,
    bool success,
    uint32_t error_status,
    uint32_t now_millis,
    uint32_t retry_period_millis)
{
    if ((health == NULL) || !health->recovery_pending ||
        (retry_period_millis == 0u))
    {
        return;
    }

    if (success)
    {
        health->ready = true;
        health->recovery_pending = false;
        health->consecutive_errors = 0u;
        ++health->recovery_count;
    }
    else
    {
        health->ready = false;
        ++health->error_count;
        health->last_error_status = error_status;
        if (health->consecutive_errors < UINT8_MAX)
        {
            ++health->consecutive_errors;
        }
        health->retry_after_millis = now_millis + retry_period_millis;
    }
}
