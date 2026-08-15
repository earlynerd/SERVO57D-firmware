#ifndef MKS57D_FAULT_LATCH_H
#define MKS57D_FAULT_LATCH_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    FAULT_SOURCE_NONE = 0,
    FAULT_SOURCE_INTERNAL_INVARIANT = 1u << 0,
    FAULT_SOURCE_CLOCK = 1u << 1,
    FAULT_SOURCE_CORE_EXCEPTION = 1u << 2,
    FAULT_SOURCE_UNEXPECTED_INTERRUPT = 1u << 3
} fault_source_t;

typedef struct
{
    uint32_t active;
    fault_source_t first;
} fault_latch_t;

void fault_latch_init(fault_latch_t *latch);
void fault_latch_raise(fault_latch_t *latch, fault_source_t source);
bool fault_latch_is_active(const fault_latch_t *latch);

#endif
