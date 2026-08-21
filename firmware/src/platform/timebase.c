#include "mks57d/timebase.h"

#include "mks57d/interrupt_priority.h"
#include "n32l40x.h"

static volatile uint32_t s_milliseconds;
static uint32_t s_ticks_per_microsecond;

bool timebase_init(void)
{
    if ((SystemCoreClock < 1000000u) ||
        ((SystemCoreClock % 1000000u) != 0u))
    {
        return false;
    }

    s_milliseconds = 0u;
    s_ticks_per_microsecond = SystemCoreClock / 1000000u;
    if (SysTick_Config(SystemCoreClock / 1000u) != 0u)
    {
        return false;
    }

    NVIC_SetPriority(SysTick_IRQn, INTERRUPT_PRIORITY_TIMEKEEPING);
    return (NVIC_GetPriority(SysTick_IRQn) ==
            INTERRUPT_PRIORITY_TIMEKEEPING) &&
           (NVIC_GetPriorityGrouping() ==
            INTERRUPT_PRIORITY_GROUP_ALL_PREEMPT);
}

uint32_t timebase_millis(void)
{
    return s_milliseconds;
}

uint32_t timebase_micros(void)
{
    uint32_t milliseconds;
    uint32_t counter_ticks;
    uint32_t systick_pending;

    for (;;)
    {
        milliseconds = s_milliseconds;
        counter_ticks = SysTick->VAL;
        systick_pending = SCB->ICSR & SCB_ICSR_PENDSTSET_Msk;
        if (milliseconds != s_milliseconds)
        {
            continue;
        }
        if (systick_pending != 0u)
        {
            counter_ticks = SysTick->VAL;
            if (milliseconds != s_milliseconds)
            {
                continue;
            }
            ++milliseconds;
        }
        break;
    }

    return (milliseconds * 1000u) +
           ((SysTick->LOAD - counter_ticks) /
            s_ticks_per_microsecond);
}

void SysTick_Handler(void)
{
    ++s_milliseconds;
}
