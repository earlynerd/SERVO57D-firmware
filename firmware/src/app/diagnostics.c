#include "mks57d/diagnostics.h"

#include <stddef.h>

#include "mks57d/panic.h"
#include "mks57d/platform.h"
#include "n32l40x.h"

#if !defined(MKS57D_FIRMWARE_VERSION_MAJOR) || \
    !defined(MKS57D_FIRMWARE_VERSION_MINOR) || \
    !defined(MKS57D_FIRMWARE_VERSION_PATCH)
#error "Firmware version components must be supplied by CMake"
#endif

#define PACK_FIRMWARE_VERSION(major, minor, patch) \
    ((((uint32_t)(major) & 0xFFu) << 24) |        \
     (((uint32_t)(minor) & 0xFFu) << 16) |        \
     ((uint32_t)(patch) & 0xFFFFu))

enum
{
    DIAGNOSTICS_EXPECTED_RECORD_SIZE = 64u,
    DIAGNOSTICS_EXPECTED_SEQUENCE_OFFSET = 12u
};

_Static_assert(sizeof(diagnostics_record_t) == DIAGNOSTICS_EXPECTED_RECORD_SIZE,
               "diagnostics ABI size changed without a schema review");
_Static_assert(offsetof(diagnostics_record_t, sequence) ==
                   DIAGNOSTICS_EXPECTED_SEQUENCE_OFFSET,
               "diagnostics ABI sequence offset changed");

volatile diagnostics_record_t g_diagnostics;

static uint32_t diagnostics_begin_update(void)
{
    const uint32_t odd_sequence = (g_diagnostics.sequence + 1u) | 1u;

    g_diagnostics.sequence = odd_sequence;
    __DMB();
    return odd_sequence;
}

static void diagnostics_end_update(uint32_t odd_sequence)
{
    __DMB();
    g_diagnostics.sequence = odd_sequence + 1u;
    __DMB();
}

void diagnostics_init(uint32_t app_state,
                      uint32_t uptime_millis,
                      uint32_t heartbeat_count,
                      uint32_t watchdog_status,
                      const boot_self_test_t *self_test)
{
    const panic_code_t retained_panic = g_last_panic;
    const uint32_t odd_sequence = diagnostics_begin_update();

    g_diagnostics.magic = DIAGNOSTICS_RECORD_MAGIC;
    g_diagnostics.schema_version = DIAGNOSTICS_RECORD_SCHEMA_VERSION;
    g_diagnostics.record_size = sizeof(g_diagnostics);
    g_diagnostics.firmware_version = PACK_FIRMWARE_VERSION(
        MKS57D_FIRMWARE_VERSION_MAJOR,
        MKS57D_FIRMWARE_VERSION_MINOR,
        MKS57D_FIRMWARE_VERSION_PATCH);
    g_diagnostics.capabilities = DIAGNOSTICS_CAPABILITY_PASSIVE_IMAGE |
                                 DIAGNOSTICS_CAPABILITY_STATUS_LED |
                                 DIAGNOSTICS_CAPABILITY_IWDG |
                                 DIAGNOSTICS_CAPABILITY_RESET_CAUSE |
                                 DIAGNOSTICS_CAPABILITY_PRIORITY_POLICY;
    g_diagnostics.app_state = app_state;
    g_diagnostics.uptime_millis = uptime_millis;
    g_diagnostics.heartbeat_count = heartbeat_count;
    g_diagnostics.watchdog_status = watchdog_status;
    g_diagnostics.platform_boot_status =
        (uint32_t)g_platform_boot_diagnostics.status;
    g_diagnostics.reset_flags = g_platform_boot_diagnostics.reset_flags;
    g_diagnostics.retained_panic = (uint32_t)retained_panic;
    g_diagnostics.self_test_required = self_test->required;
    g_diagnostics.self_test_passed = self_test->passed;
    g_diagnostics.self_test_failed = self_test->failed;

    diagnostics_end_update(odd_sequence);

    /* The diagnostic record now owns the preceding boot's retained history. */
    g_last_panic = PANIC_NONE;
    __DMB();
}

void diagnostics_publish(uint32_t app_state,
                         uint32_t uptime_millis,
                         uint32_t heartbeat_count,
                         uint32_t watchdog_status,
                         const boot_self_test_t *self_test)
{
    const uint32_t odd_sequence = diagnostics_begin_update();

    g_diagnostics.app_state = app_state;
    g_diagnostics.uptime_millis = uptime_millis;
    g_diagnostics.heartbeat_count = heartbeat_count;
    g_diagnostics.watchdog_status = watchdog_status;
    g_diagnostics.platform_boot_status =
        (uint32_t)g_platform_boot_diagnostics.status;
    g_diagnostics.self_test_required = self_test->required;
    g_diagnostics.self_test_passed = self_test->passed;
    g_diagnostics.self_test_failed = self_test->failed;

    diagnostics_end_update(odd_sequence);
}
