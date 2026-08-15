#include "mks57d/timebase.h"

#include "n32l40x.h"

static volatile uint32_t s_milliseconds;

bool timebase_init(void)
{
    if (SystemCoreClock < 1000u)
    {
        return false;
    }

    s_milliseconds = 0u;
    return SysTick_Config(SystemCoreClock / 1000u) == 0u;
}

uint32_t timebase_millis(void)
{
    return s_milliseconds;
}

void SysTick_Handler(void)
{
    ++s_milliseconds;
}
