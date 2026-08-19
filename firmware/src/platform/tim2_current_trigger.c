#include "mks57d/tim2_current_trigger.h"

#include "mks57d/adc1.h"
#include "mks57d/interrupt_priority.h"
#include "n32l40x.h"

enum
{
    TIMER_MAX_PERIOD_COUNTS = 65536u,
    TIM2_RESET_FROM_TIM3_UPDATE = TIM_SMCTRL_TSEL_1 |
                                  TIM_SMCTRL_SMSEL_2
};

bool tim2_current_trigger_init(uint32_t timer_clock_hz)
{
    uint32_t period_counts;
    uint32_t compare_counts;
    uint32_t wait_budget = 10000u;

    NVIC_DisableIRQ(TIM2_IRQn);
    if ((timer_clock_hz == 0u) ||
        ((timer_clock_hz % TIM2_CURRENT_TRIGGER_FREQUENCY_HZ) != 0u))
    {
        return false;
    }

    period_counts = timer_clock_hz /
                    TIM2_CURRENT_TRIGGER_FREQUENCY_HZ;
    compare_counts =
        ((period_counts * TIM2_CURRENT_TRIGGER_PHASE_PERMILLE) + 500u) /
        1000u;
    if ((period_counts < 2u) ||
        (period_counts > TIMER_MAX_PERIOD_COUNTS) ||
        (compare_counts == 0u) ||
        (compare_counts >= period_counts))
    {
        return false;
    }

    RCC->APB1PCLKEN |= RCC_APB1PCLKEN_TIM2EN;
    __DSB();
    RCC->APB1PRST |= RCC_APB1PRST_TIM2RST;
    RCC->APB1PRST &= ~((uint32_t)RCC_APB1PRST_TIM2RST);
    __DSB();

    TIM2->CTRL1 = 0u;
    TIM2->CTRL2 = 0u;
    TIM2->SMCTRL = TIM2_RESET_FROM_TIM3_UPDATE;
    TIM2->DINTEN = 0u;
    TIM2->CCEN = 0u;
    TIM2->CNT = 0u;
    TIM2->PSC = 0u;
    TIM2->AR = (uint16_t)(period_counts - 1u);
    TIM2->CCDAT2 = (uint16_t)compare_counts;
    TIM2->CCMOD1 = 0u;
    TIM2->CCEN = TIM_CCEN_CC2EN;
    TIM2->EVTGEN = TIM_EVTGEN_UDGN;
    TIM2->STS = ~((uint32_t)(TIM_STS_UDITF | TIM_STS_CC2ITF));

    NVIC_ClearPendingIRQ(TIM2_IRQn);
    NVIC_SetPriority(TIM2_IRQn, INTERRUPT_PRIORITY_FAST_CURRENT);
    TIM2->DINTEN = TIM_DINTEN_CC2IEN;
    NVIC_EnableIRQ(TIM2_IRQn);
    TIM2->CTRL1 = TIM_CTRL1_ARPEN | TIM_CTRL1_CNTEN;
    DBG->CTRL &= ~((uint32_t)DBG_CTRL_TIM2_STOP);
    __DSB();

    while ((TIM2->CNT == 0u) && (wait_budget != 0u))
    {
        --wait_budget;
    }
    if (wait_budget == 0u)
    {
        TIM2->DINTEN = 0u;
        TIM2->CTRL1 = 0u;
        NVIC_DisableIRQ(TIM2_IRQn);
        return false;
    }

    return (TIM2->PSC == 0u) &&
           (TIM2->AR == (uint16_t)(period_counts - 1u)) &&
           (TIM2->CCDAT2 == (uint16_t)compare_counts) &&
           ((TIM2->SMCTRL & (TIM_SMCTRL_TSEL | TIM_SMCTRL_SMSEL)) ==
            TIM2_RESET_FROM_TIM3_UPDATE) &&
           ((TIM2->CCEN & TIM_CCEN_CC2EN) != 0u) &&
           ((TIM2->DINTEN & TIM_DINTEN_CC2IEN) != 0u) &&
           ((TIM2->CTRL1 & TIM_CTRL1_CNTEN) != 0u);
}

void TIM2_IRQHandler(void)
{
    const uint32_t status = TIM2->STS;

    TIM2->STS = ~((uint32_t)TIM_STS_CC2ITF);
    if ((status & TIM_STS_CC2ITF) != 0u)
    {
        (void)adc1_trigger_synchronized_current_from_isr();
    }
}
