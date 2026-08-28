#ifndef MKS57D_RUNTIME_PROFILE_H
#define MKS57D_RUNTIME_PROFILE_H

#include <stdbool.h>
#include <stdint.h>

enum
{
    RUNTIME_PROFILE_SCHEMA_VERSION = 1u,
    RUNTIME_PROFILE_TARGET_RELEASE_COUNT = 256u
};

typedef enum
{
    RUNTIME_PROFILE_STATE_IDLE = 0u,
    RUNTIME_PROFILE_STATE_ARMED,
    RUNTIME_PROFILE_STATE_COMPLETE
} runtime_profile_state_t;

typedef enum
{
    RUNTIME_PROFILE_METRIC_PEND_TO_ENTRY = 0u,
    RUNTIME_PROFILE_METRIC_DISPATCH,
    RUNTIME_PROFILE_METRIC_ENCODER_DECODE,
    RUNTIME_PROFILE_METRIC_ESTIMATOR,
    RUNTIME_PROFILE_METRIC_CONTROL,
    RUNTIME_PROFILE_METRIC_PUBLICATION,
    RUNTIME_PROFILE_METRIC_PENDSV_TOTAL,
    RUNTIME_PROFILE_METRIC_FOREGROUND,
    RUNTIME_PROFILE_METRIC_COUNT
} runtime_profile_metric_index_t;

typedef struct
{
    uint32_t total_cycles;
    uint32_t maximum_cycles;
} runtime_profile_metric_t;

typedef struct
{
    uint8_t schema_version;
    uint8_t state;
    uint16_t captured_release_count;
    uint16_t incomplete_release_count;
    uint16_t foreground_sample_count;
    uint32_t current_loop_completion_count;
    uint16_t maximum_current_loop_completions_per_release;
    runtime_profile_metric_t metrics[RUNTIME_PROFILE_METRIC_COUNT];
} runtime_profile_snapshot_t;

bool runtime_profile_arm(void);
bool runtime_profile_is_armed(void);
void runtime_profile_deferred_pended(uint32_t cycle_count);
bool runtime_profile_pendsv_begin(
    uint32_t cycle_count,
    uint32_t current_loop_completion_count);
bool runtime_profile_release_active(void);
void runtime_profile_callback_begin(uint32_t cycle_count);
void runtime_profile_encoder_decode_complete(uint32_t cycle_count);
void runtime_profile_estimator_complete(uint32_t cycle_count);
void runtime_profile_control_complete(uint32_t cycle_count);
void runtime_profile_callback_complete(uint32_t cycle_count);
void runtime_profile_pendsv_complete(
    uint32_t cycle_count,
    uint32_t current_loop_completion_count);
void runtime_profile_foreground_complete(
    uint32_t start_cycle_count,
    uint32_t end_cycle_count);
bool runtime_profile_get_snapshot(runtime_profile_snapshot_t* snapshot);

#endif
