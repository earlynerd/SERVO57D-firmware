#include "mks57d/cycle_counter.h"

#include "n32l40x.h"

bool cycle_counter_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    __DSB();

    if ((DWT->CTRL & DWT_CTRL_NOCYCCNT_Msk) != 0u)
    {
        return false;
    }

    if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0u)
    {
        return true;
    }

    DWT->CYCCNT = 0u;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    __DSB();
    __ISB();
    return (DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0u;
}

uint32_t cycle_counter_read(void)
{
    return DWT->CYCCNT;
}
