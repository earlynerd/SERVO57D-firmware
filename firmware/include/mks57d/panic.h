#ifndef MKS57D_PANIC_H
#define MKS57D_PANIC_H

#include <stdbool.h>
#include <stdint.h>

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
    PANIC_PASSIVE_BOARD_INVARIANT,
    PANIC_TIMEBASE_INIT,
    PANIC_WATCHDOG_INIT,
    PANIC_WATCHDOG_LIVENESS,
    PANIC_INTERNAL_INVARIANT,
    PANIC_DRIVE_CONTROL,
    PANIC_CODE_COUNT
} panic_code_t;

enum
{
    PLATFORM_FAULT_RECORD_SCHEMA_VERSION = 1u,
    PLATFORM_FAULT_RECORD_FLAG_EXCEPTION_FRAME_VALID = 1u << 0
};

typedef struct
{
    uint32_t schema_version;
    uint32_t flags;
    uint32_t panic_code;
    uint32_t exception_number;
    uint32_t exception_return;
    uint32_t stacked_program_counter;
    uint32_t stacked_link_register;
    uint32_t stacked_xpsr;
    uint32_t main_stack_pointer;
    uint32_t process_stack_pointer;
    uint32_t configurable_fault_status;
    uint32_t hard_fault_status;
    uint32_t debug_fault_status;
    uint32_t memory_management_fault_address;
    uint32_t bus_fault_address;
} platform_fault_record_t;

extern volatile panic_code_t g_last_panic;

_Noreturn void platform_panic(panic_code_t code);
_Noreturn void platform_unexpected_interrupt(void);
bool platform_fault_record_consume(void);
const volatile platform_fault_record_t* platform_fault_record_current(void);

#endif
