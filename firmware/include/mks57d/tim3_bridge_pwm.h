#ifndef MKS57D_TIM3_BRIDGE_PWM_H
#define MKS57D_TIM3_BRIDGE_PWM_H

#include <stdbool.h>
#include <stdint.h>

enum
{
    TIM3_BRIDGE_PWM_CHANNEL_COUNT = 4u,
    TIM3_BRIDGE_PWM_FREQUENCY_HZ = 20000u,
    TIM3_BRIDGE_PWM_ACTIVE_DUTY_PERMILLE = 500u
};

bool tim3_bridge_pwm_init(uint32_t timer_clock_hz);
bool tim3_bridge_pwm_apply(uint32_t selected_channel, bool active);
bool tim3_bridge_pwm_zero(void);
void tim3_bridge_pwm_stop(void);

#endif
