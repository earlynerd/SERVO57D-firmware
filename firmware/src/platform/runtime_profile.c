#include "mks57d/runtime_profile.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

static runtime_profile_snapshot_t s_snapshot;
static volatile bool s_armed;
static bool s_pended_valid;
static bool s_release_active;
static bool s_release_pended_valid;
static bool s_callback_valid;
static bool s_decode_valid;
static bool s_estimator_valid;
static bool s_control_valid;
static bool s_callback_complete_valid;
static uint32_t s_pended_cycle_count;
static uint32_t s_release_pended_cycle_count;
static uint32_t s_pendsv_entry_cycle_count;
static uint32_t s_callback_cycle_count;
static uint32_t s_decode_cycle_count;
static uint32_t s_estimator_cycle_count;
static uint32_t s_control_cycle_count;
static uint32_t s_callback_complete_cycle_count;
static uint32_t s_current_loop_completion_count;

static uint32_t saturating_add_u32(uint32_t left, uint32_t right)
{
    return right > (UINT32_MAX - left) ? UINT32_MAX : left + right;
}

static uint16_t saturating_increment_u16(uint16_t value)
{
    return value == UINT16_MAX ? UINT16_MAX : (uint16_t)(value + 1u);
}

static uint16_t saturate_u16(uint32_t value)
{
    return value > UINT16_MAX ? UINT16_MAX : (uint16_t)value;
}

static void record_metric(runtime_profile_metric_index_t index,
                          uint32_t cycles)
{
    runtime_profile_metric_t* metric;

    if ((uint32_t)index >= (uint32_t)RUNTIME_PROFILE_METRIC_COUNT)
    {
        return;
    }
    metric = &s_snapshot.metrics[index];
    metric->total_cycles = saturating_add_u32(
        metric->total_cycles, cycles);
    if (cycles > metric->maximum_cycles)
    {
        metric->maximum_cycles = cycles;
    }
}

bool runtime_profile_arm(void)
{
    if (s_armed)
    {
        return false;
    }

    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_snapshot.schema_version = RUNTIME_PROFILE_SCHEMA_VERSION;
    s_snapshot.state = RUNTIME_PROFILE_STATE_ARMED;
    s_pended_valid = false;
    s_release_active = false;
    s_armed = true;
    return true;
}

bool runtime_profile_is_armed(void)
{
    return s_armed;
}

void runtime_profile_deferred_pended(uint32_t cycle_count)
{
    if (s_armed && !s_pended_valid)
    {
        s_pended_cycle_count = cycle_count;
        s_pended_valid = true;
    }
}

bool runtime_profile_pendsv_begin(
    uint32_t cycle_count,
    uint32_t current_loop_completion_count)
{
    if (!s_armed || s_release_active)
    {
        return false;
    }

    s_release_active = true;
    s_release_pended_valid =
        s_pended_valid &&
        ((int32_t)(cycle_count - s_pended_cycle_count) >= 0);
    if (s_release_pended_valid)
    {
        s_release_pended_cycle_count = s_pended_cycle_count;
        s_pended_valid = false;
    }
    s_callback_valid = false;
    s_decode_valid = false;
    s_estimator_valid = false;
    s_control_valid = false;
    s_callback_complete_valid = false;
    s_pendsv_entry_cycle_count = cycle_count;
    s_current_loop_completion_count = current_loop_completion_count;
    return true;
}

bool runtime_profile_release_active(void)
{
    return s_release_active;
}

void runtime_profile_callback_begin(uint32_t cycle_count)
{
    if (s_release_active)
    {
        s_callback_cycle_count = cycle_count;
        s_callback_valid = true;
    }
}

void runtime_profile_encoder_decode_complete(uint32_t cycle_count)
{
    if (s_release_active && s_callback_valid)
    {
        s_decode_cycle_count = cycle_count;
        s_decode_valid = true;
    }
}

void runtime_profile_estimator_complete(uint32_t cycle_count)
{
    if (s_release_active && s_decode_valid)
    {
        s_estimator_cycle_count = cycle_count;
        s_estimator_valid = true;
    }
}

void runtime_profile_control_complete(uint32_t cycle_count)
{
    if (s_release_active && s_estimator_valid)
    {
        s_control_cycle_count = cycle_count;
        s_control_valid = true;
    }
}

void runtime_profile_callback_complete(uint32_t cycle_count)
{
    if (s_release_active && s_control_valid)
    {
        s_callback_complete_cycle_count = cycle_count;
        s_callback_complete_valid = true;
    }
}

void runtime_profile_pendsv_complete(
    uint32_t cycle_count,
    uint32_t current_loop_completion_count)
{
    uint32_t current_loop_completions;

    if (!s_release_active)
    {
        return;
    }

    current_loop_completions =
        current_loop_completion_count - s_current_loop_completion_count;
    s_snapshot.current_loop_completion_count = saturating_add_u32(
        s_snapshot.current_loop_completion_count,
        current_loop_completions);
    if (current_loop_completions >
        s_snapshot.maximum_current_loop_completions_per_release)
    {
        s_snapshot.maximum_current_loop_completions_per_release =
            saturate_u16(current_loop_completions);
    }

    if (s_release_pended_valid && s_callback_valid && s_decode_valid &&
        s_estimator_valid && s_control_valid && s_callback_complete_valid)
    {
        record_metric(
            RUNTIME_PROFILE_METRIC_PEND_TO_ENTRY,
            s_pendsv_entry_cycle_count - s_release_pended_cycle_count);
        record_metric(
            RUNTIME_PROFILE_METRIC_DISPATCH,
            s_callback_cycle_count - s_pendsv_entry_cycle_count);
        record_metric(
            RUNTIME_PROFILE_METRIC_ENCODER_DECODE,
            s_decode_cycle_count - s_callback_cycle_count);
        record_metric(
            RUNTIME_PROFILE_METRIC_ESTIMATOR,
            s_estimator_cycle_count - s_decode_cycle_count);
        record_metric(
            RUNTIME_PROFILE_METRIC_CONTROL,
            s_control_cycle_count - s_estimator_cycle_count);
        record_metric(
            RUNTIME_PROFILE_METRIC_PUBLICATION,
            s_callback_complete_cycle_count - s_control_cycle_count);
        record_metric(
            RUNTIME_PROFILE_METRIC_PENDSV_TOTAL,
            cycle_count - s_pendsv_entry_cycle_count);
    }
    else
    {
        s_snapshot.incomplete_release_count = saturating_increment_u16(
            s_snapshot.incomplete_release_count);
    }

    s_snapshot.captured_release_count = saturating_increment_u16(
        s_snapshot.captured_release_count);
    s_release_active = false;
    if (s_snapshot.captured_release_count >=
        RUNTIME_PROFILE_TARGET_RELEASE_COUNT)
    {
        s_snapshot.state = RUNTIME_PROFILE_STATE_COMPLETE;
        s_armed = false;
        s_pended_valid = false;
    }
}

void runtime_profile_foreground_complete(
    uint32_t start_cycle_count,
    uint32_t end_cycle_count)
{
    if (!s_armed)
    {
        return;
    }

    record_metric(
        RUNTIME_PROFILE_METRIC_FOREGROUND,
        end_cycle_count - start_cycle_count);
    s_snapshot.foreground_sample_count = saturating_increment_u16(
        s_snapshot.foreground_sample_count);
}

bool runtime_profile_get_snapshot(runtime_profile_snapshot_t* snapshot)
{
    if ((snapshot == NULL) || s_armed ||
        (s_snapshot.state != RUNTIME_PROFILE_STATE_COMPLETE))
    {
        return false;
    }

    *snapshot = s_snapshot;
    return true;
}
