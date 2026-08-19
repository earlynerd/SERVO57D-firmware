#ifndef MKS57D_CURRENT_LOOP_BACKEND_H
#define MKS57D_CURRENT_LOOP_BACKEND_H

#include <stdbool.h>
#include <stdint.h>

#include "mks57d/phase_current_loop.h"

typedef enum
{
    CURRENT_LOOP_BACKEND_FAULT_NONE = 0u,
    CURRENT_LOOP_BACKEND_FAULT_PHASE_MASK = 0x0000FFFFu,
    CURRENT_LOOP_BACKEND_FAULT_ADC = 1u << 16,
    CURRENT_LOOP_BACKEND_FAULT_PWM = 1u << 17,
    CURRENT_LOOP_BACKEND_FAULT_DEADLINE = 1u << 18,
    CURRENT_LOOP_BACKEND_FAULT_INTERNAL = 1u << 19
} current_loop_backend_fault_t;

typedef struct
{
    phase_current_loop_output_t latest_output;
    int16_t current_a_reference_counts;
    int16_t current_b_reference_counts;
    uint32_t sample_count;
    uint32_t fault_flags;
    bool initialized;
    bool active;
} current_loop_backend_snapshot_t;

bool current_loop_backend_init(
    const phase_current_loop_config_t* config);
bool current_loop_backend_set_reference_counts(
    int16_t current_a_reference_counts,
    int16_t current_b_reference_counts);
bool current_loop_backend_start(void);
bool current_loop_backend_stop(void);
void current_loop_backend_get_snapshot(
    current_loop_backend_snapshot_t* snapshot);

#endif
