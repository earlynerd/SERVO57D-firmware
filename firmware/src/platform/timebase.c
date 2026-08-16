#include "mks57d/timebase.h"

#include "mks57d/interrupt_priority.h"
#include "n32l40x.h"

static volatile uint32_t s_milliseconds;

bool timebase_init(void)
{
    if (SystemCoreClock < 1000u)
    {
        return false;
    }

    s_milliseconds = 0u;
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

void SysTick_Handler(void)
{
    ++s_milliseconds;
}
