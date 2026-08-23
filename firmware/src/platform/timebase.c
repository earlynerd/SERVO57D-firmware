#include "mks57d/timebase.h"

#include "mks57d/interrupt_priority.h"
#include "mks57d/timebase_reconcile.h"
#include "n32l40x.h"

enum
{
    MICROSECONDS_PER_MILLISECOND = 1000u,
    TIMEBASE_SAMPLE_MAXIMUM_ATTEMPTS = 4u,
    TIMEBASE_PUBLICATION_MAXIMUM_ATTEMPTS = 4u
};

static volatile uint32_t s_milliseconds;
static volatile uint32_t s_last_microseconds;
static uint32_t s_ticks_per_microsecond;

bool timebase_init(void)
{
    if ((SystemCoreClock < 1000000u) ||
        ((SystemCoreClock % 1000000u) != 0u))
    {
        return false;
    }

    s_milliseconds = 0u;
    s_last_microseconds = 0u;
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

static bool sample_raw_microseconds(uint32_t* sampled_microseconds)
{
    uint32_t attempt;
    uint32_t milliseconds;
    uint32_t counter_ticks;
    uint32_t systick_pending;

    for (attempt = 0u;
         attempt < TIMEBASE_SAMPLE_MAXIMUM_ATTEMPTS;
         ++attempt)
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

        *sampled_microseconds =
            (milliseconds * MICROSECONDS_PER_MILLISECOND) +
            ((SysTick->LOAD - counter_ticks) /
             s_ticks_per_microsecond);
        return true;
    }

    return false;
}

uint32_t timebase_micros(void)
{
    uint32_t attempt;

    for (attempt = 0u;
         attempt < TIMEBASE_PUBLICATION_MAXIMUM_ATTEMPTS;
         ++attempt)
    {
        const uint32_t previous = __LDREXW(&s_last_microseconds);
        uint32_t sampled;
        uint32_t reconciled;

        if (!sample_raw_microseconds(&sampled))
        {
            __CLREX();
            __DMB();
            return s_last_microseconds;
        }
        reconciled = timebase_reconcile_microseconds(
            previous, sampled, MICROSECONDS_PER_MILLISECOND);

        if (__STREXW(reconciled, &s_last_microseconds) == 0u)
        {
            __DMB();
            return reconciled;
        }
    }

    /*
     * Nested interrupts can invalidate an exclusive reservation. Do not spin
     * in an ISR: the successful publisher already left a safe timestamp.
     */
    __CLREX();
    __DMB();
    return s_last_microseconds;
}

void SysTick_Handler(void)
{
    ++s_milliseconds;
}
