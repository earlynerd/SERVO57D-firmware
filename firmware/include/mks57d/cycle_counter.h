#ifndef MKS57D_CYCLE_COUNTER_H
#define MKS57D_CYCLE_COUNTER_H

#include <stdbool.h>
#include <stdint.h>

/* Enables the Cortex-M4 DWT cycle counter after the final core clock is set. */
bool cycle_counter_init(void);
uint32_t cycle_counter_read(void);

#endif
