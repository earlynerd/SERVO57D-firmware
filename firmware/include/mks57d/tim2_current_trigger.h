#ifndef MKS57D_TIM2_CURRENT_TRIGGER_H
#define MKS57D_TIM2_CURRENT_TRIGGER_H

#include <stdbool.h>
#include <stdint.h>

enum
{
    TIM2_CURRENT_TRIGGER_FREQUENCY_HZ = 20000u,
    TIM2_CURRENT_TRIGGER_PHASE_PERMILLE = 300u
};

bool tim2_current_trigger_init(uint32_t timer_clock_hz);

#endif
