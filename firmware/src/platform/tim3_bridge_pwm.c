#include "mks57d/tim3_bridge_pwm.h"

#include <stddef.h>

#include "n32l40x.h"

enum
{
    TIMER_MAX_PERIOD_COUNTS = 65536u,
    PWM1_WITH_PRELOAD_CH1 = TIM_CCMOD1_OC1M_1 |
                            TIM_CCMOD1_OC1M_2 |
                            TIM_CCMOD1_OC1PEN,
    PWM1_WITH_PRELOAD_CH2 = TIM_CCMOD1_OC2M_1 |
                            TIM_CCMOD1_OC2M_2 |
                            TIM_CCMOD1_OC2PEN,
    PWM1_WITH_PRELOAD_CH3 = TIM_CCMOD2_OC3MD_1 |
                            TIM_CCMOD2_OC3MD_2 |
                            TIM_CCMOD2_OC3PEN,
    PWM1_WITH_PRELOAD_CH4 = TIM_CCMOD2_OC4MD_1 |
                            TIM_CCMOD2_OC4MD_2 |
                            TIM_CCMOD2_OC4PEN,
    ALL_CHANNEL_OUTPUTS_ENABLED = TIM_CCEN_CC1EN |
                                  TIM_CCEN_CC2EN |
                                  TIM_CCEN_CC3EN |
                                  TIM_CCEN_CC4EN,
    MASTER_TRIGGER_ON_UPDATE = TIM_CTRL2_MMSEL_1
};

static uint16_t s_active_compare;
static bool s_initialized;

static void write_compares(uint32_t selected_channel, uint16_t selected_compare)
{
    TIM3->CCDAT1 = selected_channel == 0u ? selected_compare : 0u;
    TIM3->CCDAT2 = selected_channel == 1u ? selected_compare : 0u;
    TIM3->CCDAT3 = selected_channel == 2u ? selected_compare : 0u;
    TIM3->CCDAT4 = selected_channel == 3u ? selected_compare : 0u;
}

static bool compares_match(uint32_t selected_channel,
                           uint16_t selected_compare)
{
    return (TIM3->CCDAT1 ==
            (selected_channel == 0u ? selected_compare : 0u)) &&
           (TIM3->CCDAT2 ==
            (selected_channel == 1u ? selected_compare : 0u)) &&
           (TIM3->CCDAT3 ==
            (selected_channel == 2u ? selected_compare : 0u)) &&
           (TIM3->CCDAT4 ==
            (selected_channel == 3u ? selected_compare : 0u));
}

bool tim3_bridge_pwm_init(uint32_t timer_clock_hz)
{
    uint32_t period_counts;

    s_initialized = false;
    s_active_compare = 0u;

    if ((timer_clock_hz == 0u) ||
        ((timer_clock_hz % TIM3_BRIDGE_PWM_FREQUENCY_HZ) != 0u))
    {
        return false;
    }

    period_counts = timer_clock_hz / TIM3_BRIDGE_PWM_FREQUENCY_HZ;
    if ((period_counts < 2u) ||
        (period_counts > TIMER_MAX_PERIOD_COUNTS) ||
        ((period_counts & 1u) != 0u))
    {
        return false;
    }

    RCC->APB1PCLKEN |= RCC_APB1PCLKEN_TIM3EN;
    __DSB();
    RCC->APB1PRST |= RCC_APB1PRST_TIM3RST;
    RCC->APB1PRST &= ~RCC_APB1PRST_TIM3RST;
    __DSB();

    TIM3->CTRL1 = 0u;
    TIM3->CTRL2 = MASTER_TRIGGER_ON_UPDATE;
    TIM3->SMCTRL = 0u;
    TIM3->DINTEN = 0u;
    TIM3->CCEN = 0u;
    TIM3->CNT = 0u;
    TIM3->PSC = 0u;
    TIM3->AR = (uint16_t)(period_counts - 1u);
    TIM3->CCMOD1 = (uint16_t)(PWM1_WITH_PRELOAD_CH1 |
                              PWM1_WITH_PRELOAD_CH2);
    TIM3->CCMOD2 = (uint16_t)(PWM1_WITH_PRELOAD_CH3 |
                              PWM1_WITH_PRELOAD_CH4);
    write_compares(TIM3_BRIDGE_PWM_CHANNEL_COUNT, 0u);
    TIM3->CCEN = ALL_CHANNEL_OUTPUTS_ENABLED;
    TIM3->CTRL1 = TIM_CTRL1_ARPEN;
    TIM3->EVTGEN = TIM_EVTGEN_UDGN;
    TIM3->STS = ~((uint32_t)TIM_STS_UDITF);
    TIM3->CTRL1 |= TIM_CTRL1_CNTEN;
    DBG->CTRL &= ~((uint32_t)DBG_CTRL_TIM3_STOP);
    __DSB();

    s_active_compare = (uint16_t)(period_counts / 2u);
    s_initialized =
        (TIM3->PSC == 0u) &&
        (TIM3->AR == (uint16_t)(period_counts - 1u)) &&
        ((TIM3->CTRL1 & (TIM_CTRL1_CNTEN | TIM_CTRL1_ARPEN)) ==
         (TIM_CTRL1_CNTEN | TIM_CTRL1_ARPEN)) &&
        ((TIM3->CTRL2 & TIM_CTRL2_MMSEL) ==
         MASTER_TRIGGER_ON_UPDATE) &&
        ((TIM3->CCEN & ALL_CHANNEL_OUTPUTS_ENABLED) ==
         ALL_CHANNEL_OUTPUTS_ENABLED) &&
        compares_match(TIM3_BRIDGE_PWM_CHANNEL_COUNT, 0u);
    return s_initialized;
}

bool tim3_bridge_pwm_apply(uint32_t selected_channel, bool active)
{
    const uint16_t compare = active ? s_active_compare : 0u;

    if (!s_initialized ||
        (selected_channel >= TIM3_BRIDGE_PWM_CHANNEL_COUNT))
    {
        return false;
    }

    write_compares(selected_channel, compare);
    TIM3->EVTGEN = TIM_EVTGEN_UDGN;
    TIM3->STS = ~((uint32_t)TIM_STS_UDITF);
    __DSB();

    return compares_match(selected_channel, compare) &&
           ((TIM3->CTRL1 & TIM_CTRL1_CNTEN) != 0u) &&
           ((TIM3->CCEN & ALL_CHANNEL_OUTPUTS_ENABLED) ==
            ALL_CHANNEL_OUTPUTS_ENABLED);
}

bool tim3_bridge_pwm_zero(void)
{
    if (!s_initialized)
    {
        return false;
    }

    write_compares(TIM3_BRIDGE_PWM_CHANNEL_COUNT, 0u);
    TIM3->EVTGEN = TIM_EVTGEN_UDGN;
    TIM3->STS = ~((uint32_t)TIM_STS_UDITF);
    __DSB();
    return compares_match(TIM3_BRIDGE_PWM_CHANNEL_COUNT, 0u);
}

void tim3_bridge_pwm_stop(void)
{
    s_initialized = false;
    s_active_compare = 0u;

    if ((RCC->APB1PCLKEN & RCC_APB1PCLKEN_TIM3EN) == 0u)
    {
        return;
    }

    TIM3->CCDAT1 = 0u;
    TIM3->CCDAT2 = 0u;
    TIM3->CCDAT3 = 0u;
    TIM3->CCDAT4 = 0u;
    TIM3->CCEN = 0u;
    TIM3->CTRL1 &= ~((uint32_t)TIM_CTRL1_CNTEN);
    __DSB();
}
