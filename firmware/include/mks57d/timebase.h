#ifndef MKS57D_TIMEBASE_H
#define MKS57D_TIMEBASE_H

#include <stdbool.h>
#include <stdint.h>

bool timebase_init(void);
uint32_t timebase_millis(void);

#endif
