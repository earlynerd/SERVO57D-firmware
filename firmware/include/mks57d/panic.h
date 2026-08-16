#ifndef MKS57D_PANIC_H
#define MKS57D_PANIC_H

typedef enum
{
    PANIC_NONE = 0,
    PANIC_NMI,
    PANIC_HARD_FAULT,
    PANIC_MEMORY_FAULT,
    PANIC_BUS_FAULT,
    PANIC_USAGE_FAULT,
    PANIC_UNEXPECTED_INTERRUPT,
    PANIC_EARLY_PLATFORM_INIT,
    PANIC_CLOCK_INIT,
    PANIC_INTERRUPT_PRIORITY_INIT,
    PANIC_TIMEBASE_INIT,
    PANIC_WATCHDOG_INIT,
    PANIC_WATCHDOG_LIVENESS,
    PANIC_INTERNAL_INVARIANT,
    PANIC_CODE_COUNT
} panic_code_t;

extern volatile panic_code_t g_last_panic;

_Noreturn void platform_panic(panic_code_t code);
_Noreturn void platform_unexpected_interrupt(void);

#endif
