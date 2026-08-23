#ifndef MKS57D_TIM2_CURRENT_TRIGGER_H
#define MKS57D_TIM2_CURRENT_TRIGGER_H

#include <stdbool.h>
#include <stdint.h>

enum
{
    TIM2_CURRENT_TRIGGER_FREQUENCY_HZ = 20000u,
    TIM2_CURRENT_TRIGGER_PHASE_PERMILLE = 800u
};

bool tim2_current_trigger_init(uint32_t timer_clock_hz);
uint16_t tim2_current_trigger_counter(void);
uint16_t tim2_current_trigger_elapsed_ticks(uint16_t start, uint16_t end);

#endif
