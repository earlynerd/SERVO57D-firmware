#include "mks57d/interrupt_priority.h"

#include "n32l40x.h"

enum
{
    INTERRUPT_PRIORITY_LEVEL_COUNT = 1u << __NVIC_PRIO_BITS
};

_Static_assert(__NVIC_PRIO_BITS == 4u,
               "N32L406 priority policy requires four implemented NVIC bits");
_Static_assert(INTERRUPT_PRIORITY_EMERGENCY_FAULT <
                   INTERRUPT_PRIORITY_CONTROL_GUARDIAN,
               "emergency faults must preempt the deadline guardian");
_Static_assert(INTERRUPT_PRIORITY_CONTROL_GUARDIAN <
                   INTERRUPT_PRIORITY_FAST_CURRENT,
               "deadline guardian must preempt current control");
_Static_assert(INTERRUPT_PRIORITY_FAST_CURRENT <
                   INTERRUPT_PRIORITY_ROTOR_FEEDBACK,
               "current control must preempt rotor feedback");
_Static_assert(INTERRUPT_PRIORITY_COMMUNICATIONS <
                   INTERRUPT_PRIORITY_SLOW_RELEASE,
               "communications transfer must preempt optional slow release");
_Static_assert((unsigned int)INTERRUPT_PRIORITY_TIMEKEEPING <
                   (unsigned int)INTERRUPT_PRIORITY_LEVEL_COUNT,
               "timekeeping priority exceeds the implemented NVIC range");

bool interrupt_priority_init(void)
{
    NVIC_SetPriorityGrouping(INTERRUPT_PRIORITY_GROUP_ALL_PREEMPT);
    __DSB();
    __ISB();

    return NVIC_GetPriorityGrouping() ==
           INTERRUPT_PRIORITY_GROUP_ALL_PREEMPT;
}
