#ifndef MKS57D_DISPLAY_HEALTH_H
#define MKS57D_DISPLAY_HEALTH_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    uint32_t retry_after_millis;
    uint32_t error_count;
    uint32_t recovery_count;
    uint32_t last_error_status;
    uint8_t consecutive_errors;
    bool ready;
    bool recovery_pending;
} display_health_t;

bool display_health_init(display_health_t* health,
                         bool ready,
                         uint32_t initial_error_status,
                         uint32_t now_millis,
                         uint32_t retry_period_millis);
bool display_health_is_ready(const display_health_t* health);
void display_health_record_write_success(display_health_t* health);
void display_health_record_write_failure(display_health_t* health,
                                         uint32_t error_status);
bool display_health_recovery_due(const display_health_t* health,
                                 bool bridge_authorized,
                                 uint32_t now_millis);
void display_health_record_recovery_result(
    display_health_t* health,
    bool success,
    uint32_t error_status,
    uint32_t now_millis,
    uint32_t retry_period_millis);

#endif
