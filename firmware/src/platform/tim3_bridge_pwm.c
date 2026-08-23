#include "mks57d/tim3_bridge_pwm.h"

#include <stddef.h>

#include "mks57d/interrupt_priority.h"
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

static uint16_t s_period_counts;
static bool s_initialized;
static tim3_bridge_pwm_update_handler_t s_update_handler;
static void* s_update_handler_context;

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

static void write_duty_compares(
    const uint16_t compares[TIM3_BRIDGE_PWM_CHANNEL_COUNT])
{
    TIM3->CCDAT1 = compares[0];
    TIM3->CCDAT2 = compares[1];
    TIM3->CCDAT3 = compares[2];
    TIM3->CCDAT4 = compares[3];
}

static bool duty_compares_match(
    const uint16_t compares[TIM3_BRIDGE_PWM_CHANNEL_COUNT])
{
    return (TIM3->CCDAT1 == compares[0]) &&
           (TIM3->CCDAT2 == compares[1]) &&
           (TIM3->CCDAT3 == compares[2]) &&
           (TIM3->CCDAT4 == compares[3]);
}

bool tim3_bridge_pwm_init(uint32_t timer_clock_hz)
{
    uint32_t period_counts;

    s_initialized = false;
    s_period_counts = 0u;
    s_update_handler = NULL;
    s_update_handler_context = NULL;
    NVIC_DisableIRQ(TIM3_IRQn);

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

    s_period_counts = (uint16_t)period_counts;
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

bool tim3_bridge_pwm_stage_duties(
    const uint16_t duty_permille[TIM3_BRIDGE_PWM_CHANNEL_COUNT])
{
    uint16_t compares[TIM3_BRIDGE_PWM_CHANNEL_COUNT];
    uint32_t channel;

    if (!s_initialized || (duty_permille == NULL) ||
        (s_period_counts == 0u))
    {
        return false;
    }

    for (channel = 0u;
         channel < TIM3_BRIDGE_PWM_CHANNEL_COUNT;
         ++channel)
    {
        uint32_t compare;

        if (duty_permille[channel] > 1000u)
        {
            return false;
        }
        compare = ((uint32_t)s_period_counts *
                   duty_permille[channel] + 500u) / 1000u;
        if (compare > s_period_counts)
        {
            return false;
        }
        compares[channel] = (uint16_t)compare;
    }

    write_duty_compares(compares);
    __DMB();
    return duty_compares_match(compares) &&
           ((TIM3->CTRL1 & TIM_CTRL1_CNTEN) != 0u) &&
           ((TIM3->CCEN & ALL_CHANNEL_OUTPUTS_ENABLED) ==
            ALL_CHANNEL_OUTPUTS_ENABLED);
}

bool tim3_bridge_pwm_get_preload_margin_ticks(uint16_t* margin_ticks)
{
    uint16_t counter;

    if (!s_initialized || (margin_ticks == NULL) ||
        (s_period_counts == 0u))
    {
        return false;
    }

    counter = TIM3->CNT;
    if (counter >= s_period_counts)
    {
        return false;
    }
    *margin_ticks = (uint16_t)(s_period_counts - counter);
    return true;
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

bool tim3_bridge_pwm_set_update_handler(
    tim3_bridge_pwm_update_handler_t handler,
    void* context)
{
    if (!s_initialized ||
        ((TIM3->DINTEN & TIM_DINTEN_UIEN) != 0u))
    {
        return false;
    }

    s_update_handler = NULL;
    __DMB();
    s_update_handler_context = context;
    __DMB();
    s_update_handler = handler;
    __DMB();
    return true;
}

bool tim3_bridge_pwm_update_irq_enable(bool enable)
{
    if (!s_initialized)
    {
        return false;
    }

    if (!enable)
    {
        TIM3->DINTEN &= ~((uint32_t)TIM_DINTEN_UIEN);
        NVIC_DisableIRQ(TIM3_IRQn);
        NVIC_ClearPendingIRQ(TIM3_IRQn);
        TIM3->STS = ~((uint32_t)TIM_STS_UDITF);
        return true;
    }
    if (s_update_handler == NULL)
    {
        return false;
    }

    NVIC_DisableIRQ(TIM3_IRQn);
    TIM3->STS = ~((uint32_t)TIM_STS_UDITF);
    NVIC_ClearPendingIRQ(TIM3_IRQn);
    NVIC_SetPriority(TIM3_IRQn,
                     INTERRUPT_PRIORITY_CONTROL_GUARDIAN);
    TIM3->DINTEN |= TIM_DINTEN_UIEN;
    NVIC_EnableIRQ(TIM3_IRQn);
    return (TIM3->DINTEN & TIM_DINTEN_UIEN) != 0u;
}

void tim3_bridge_pwm_stop(void)
{
    s_initialized = false;
    s_period_counts = 0u;
    s_update_handler = NULL;
    s_update_handler_context = NULL;

    NVIC_DisableIRQ(TIM3_IRQn);
    NVIC_ClearPendingIRQ(TIM3_IRQn);

    if ((RCC->APB1PCLKEN & RCC_APB1PCLKEN_TIM3EN) == 0u)
    {
        return;
    }

    TIM3->CCDAT1 = 0u;
    TIM3->CCDAT2 = 0u;
    TIM3->CCDAT3 = 0u;
    TIM3->CCDAT4 = 0u;
    TIM3->DINTEN = 0u;
    TIM3->CCEN = 0u;
    TIM3->CTRL1 &= ~((uint32_t)TIM_CTRL1_CNTEN);
    __DSB();
}

void TIM3_IRQHandler(void)
{
    tim3_bridge_pwm_update_handler_t handler;
    void* context;

    if ((TIM3->STS & TIM_STS_UDITF) == 0u)
    {
        TIM3->DINTEN &= ~((uint32_t)TIM_DINTEN_UIEN);
        return;
    }

    TIM3->STS = ~((uint32_t)TIM_STS_UDITF);
    handler = s_update_handler;
    context = s_update_handler_context;
    if (handler == NULL)
    {
        TIM3->DINTEN &= ~((uint32_t)TIM_DINTEN_UIEN);
        return;
    }
    handler(context);
}
