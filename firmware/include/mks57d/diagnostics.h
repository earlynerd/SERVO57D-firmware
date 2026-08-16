#ifndef MKS57D_DIAGNOSTICS_H
#define MKS57D_DIAGNOSTICS_H

#include <stdint.h>

#include "mks57d/boot_self_test.h"

enum
{
    DIAGNOSTICS_RECORD_MAGIC = 0x4D4B5335u,
    DIAGNOSTICS_RECORD_SCHEMA_VERSION = 1u,
    DIAGNOSTICS_CAPABILITY_PASSIVE_IMAGE = 1u << 0,
    DIAGNOSTICS_CAPABILITY_STATUS_LED = 1u << 1,
    DIAGNOSTICS_CAPABILITY_IWDG = 1u << 2,
    DIAGNOSTICS_CAPABILITY_RESET_CAUSE = 1u << 3,
    DIAGNOSTICS_CAPABILITY_PRIORITY_POLICY = 1u << 4
};

typedef struct
{
    uint32_t magic;
    uint32_t schema_version;
    uint32_t record_size;
    uint32_t sequence;
    uint32_t firmware_version;
    uint32_t capabilities;
    uint32_t app_state;
    uint32_t uptime_millis;
    uint32_t heartbeat_count;
    uint32_t watchdog_status;
    uint32_t platform_boot_status;
    uint32_t reset_flags;
    uint32_t retained_panic;
    uint32_t self_test_required;
    uint32_t self_test_passed;
    uint32_t self_test_failed;
} diagnostics_record_t;

extern volatile diagnostics_record_t g_diagnostics;

void diagnostics_init(uint32_t app_state,
                      uint32_t uptime_millis,
                      uint32_t heartbeat_count,
                      uint32_t watchdog_status,
                      const boot_self_test_t *self_test);
void diagnostics_publish(uint32_t app_state,
                         uint32_t uptime_millis,
                         uint32_t heartbeat_count,
                         uint32_t watchdog_status,
                         const boot_self_test_t *self_test);

#endif
