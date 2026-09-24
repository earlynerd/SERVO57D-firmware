#include "mks57d/panic.h"

#include <stddef.h>
#include <stdint.h>

#include "mks57d/board.h"
#include "n32l40x.h"

volatile panic_code_t g_last_panic __attribute__((section(".noinit")));

enum
{
    PLATFORM_FAULT_RECORD_MAGIC = 0x4D4B5346u,
    PLATFORM_FAULT_RECORD_CURRENT_MAGIC = 0x4D4B5343u,
    PLATFORM_FAULT_RECORD_CHECKSUM_SEED = 0xC39A17E5u,
    PLATFORM_SRAM1_START = 0x20000000u,
    PLATFORM_SRAM1_END = 0x20004000u,
    PLATFORM_EXCEPTION_EXTENDED_FRAME_WORDS = 18u,
    PLATFORM_EXCEPTION_CORE_FRAME_WORDS = 8u,
    PLATFORM_EXCEPTION_RETURN_EXTENDED_FRAME = 1u << 4,
    PLATFORM_XPSR_THUMB_STATE = 1u << 24
};

typedef struct
{
    uint32_t magic;
    platform_fault_record_t record;
    uint32_t checksum;
} retained_fault_record_t;

static volatile retained_fault_record_t s_retained_fault_record
    __attribute__((section(".noinit")));

static uint32_t retained_fault_record_checksum(void)
{
    const volatile uint32_t* const words =
        (const volatile uint32_t*)&s_retained_fault_record.record;
    uint32_t checksum = PLATFORM_FAULT_RECORD_CHECKSUM_SEED;
    size_t index;

    for (index = 0u;
         index < (sizeof(s_retained_fault_record.record) / sizeof(words[0]));
         ++index)
    {
        checksum ^= words[index];
    }
    return checksum;
}

static bool exception_frame_is_readable(const uint32_t* frame,
                                        uint32_t exception_return,
                                        const uint32_t** core_frame)
{
    uintptr_t address = (uintptr_t)frame;

    if ((exception_return & PLATFORM_EXCEPTION_RETURN_EXTENDED_FRAME) == 0u)
    {
        address += PLATFORM_EXCEPTION_EXTENDED_FRAME_WORDS * sizeof(uint32_t);
    }
    if (((address & (sizeof(uint32_t) - 1u)) != 0u) ||
        (address < PLATFORM_SRAM1_START) ||
        (address > (PLATFORM_SRAM1_END -
                    PLATFORM_EXCEPTION_CORE_FRAME_WORDS * sizeof(uint32_t))))
    {
        return false;
    }
    *core_frame = (const uint32_t*)address;
    return true;
}

static void retain_fault_record(panic_code_t code,
                                const uint32_t* exception_frame,
                                uint32_t exception_return,
                                uint32_t main_stack_pointer)
{
    volatile platform_fault_record_t* const record =
        &s_retained_fault_record.record;
    const uint32_t* core_frame = NULL;

    s_retained_fault_record.magic = 0u;
    __DMB();
    record->schema_version = PLATFORM_FAULT_RECORD_SCHEMA_VERSION;
    record->flags = 0u;
    record->panic_code = (uint32_t)code;
    record->exception_number = __get_IPSR();
    record->exception_return = exception_return;
    record->stacked_program_counter = 0u;
    record->stacked_link_register = 0u;
    record->stacked_xpsr = 0u;
    record->main_stack_pointer = main_stack_pointer;
    record->process_stack_pointer = __get_PSP();
    record->configurable_fault_status = SCB->CFSR;
    record->hard_fault_status = SCB->HFSR;
    record->debug_fault_status = SCB->DFSR;
    record->memory_management_fault_address = SCB->MMFAR;
    record->bus_fault_address = SCB->BFAR;

    if ((exception_frame != NULL) &&
        exception_frame_is_readable(
            exception_frame, exception_return, &core_frame) &&
        ((core_frame[7] & PLATFORM_XPSR_THUMB_STATE) != 0u))
    {
        record->flags |= PLATFORM_FAULT_RECORD_FLAG_EXCEPTION_FRAME_VALID;
        record->stacked_link_register = core_frame[5];
        record->stacked_program_counter = core_frame[6];
        record->stacked_xpsr = core_frame[7];
    }

    s_retained_fault_record.checksum = retained_fault_record_checksum();
    __DMB();
    s_retained_fault_record.magic = PLATFORM_FAULT_RECORD_MAGIC;
    __DSB();
}

static bool retained_fault_record_is_valid(uint32_t expected_magic)
{
    if (s_retained_fault_record.magic != expected_magic)
    {
        return false;
    }
    if ((s_retained_fault_record.record.schema_version !=
         PLATFORM_FAULT_RECORD_SCHEMA_VERSION) ||
        (s_retained_fault_record.record.panic_code >=
         (uint32_t)PANIC_CODE_COUNT) ||
        (s_retained_fault_record.checksum !=
         retained_fault_record_checksum()))
    {
        s_retained_fault_record.magic = 0u;
        return false;
    }
    return true;
}

bool platform_fault_record_consume(void)
{
    if (!retained_fault_record_is_valid(PLATFORM_FAULT_RECORD_MAGIC))
    {
        s_retained_fault_record.magic = 0u;
        return false;
    }
    s_retained_fault_record.magic = PLATFORM_FAULT_RECORD_CURRENT_MAGIC;
    __DMB();
    return true;
}

const volatile platform_fault_record_t* platform_fault_record_current(void)
{
    return retained_fault_record_is_valid(
               PLATFORM_FAULT_RECORD_CURRENT_MAGIC) ?
        &s_retained_fault_record.record : NULL;
}

__attribute__((used, noinline, noreturn))
static void platform_exception_panic(panic_code_t code,
                                     const uint32_t* exception_frame,
                                     uint32_t exception_return,
                                     uint32_t main_stack_pointer)
{
    __disable_irq();
    board_bridge_force_low_zero();
    g_last_panic = code;
    retain_fault_record(
        code, exception_frame, exception_return, main_stack_pointer);

    for (;;)
    {
        __NOP();
    }
}

_Noreturn void platform_panic(panic_code_t code)
{
    __disable_irq();
    board_bridge_force_low_zero();
    g_last_panic = code;
    retain_fault_record(code, NULL, 0u, __get_MSP());

    for (;;)
    {
        __NOP();
    }
}

#define DEFINE_EXCEPTION_HANDLER(handler_name, panic_value)                 \
    __attribute__((naked, noreturn)) void handler_name(void)                \
    {                                                                        \
        __asm volatile(                                                      \
            "tst lr, #4\n"                                                  \
            "ite eq\n"                                                      \
            "mrseq r1, msp\n"                                               \
            "mrsne r1, psp\n"                                               \
            "mov r2, lr\n"                                                  \
            "mrs r3, msp\n"                                                 \
            "movs r0, %0\n"                                                 \
            "b platform_exception_panic\n"                                  \
            :                                                                \
            : "I"(panic_value));                                             \
    }

DEFINE_EXCEPTION_HANDLER(NMI_Handler, PANIC_NMI)
DEFINE_EXCEPTION_HANDLER(HardFault_Handler, PANIC_HARD_FAULT)
DEFINE_EXCEPTION_HANDLER(MemManage_Handler, PANIC_MEMORY_FAULT)
DEFINE_EXCEPTION_HANDLER(BusFault_Handler, PANIC_BUS_FAULT)
DEFINE_EXCEPTION_HANDLER(UsageFault_Handler, PANIC_USAGE_FAULT)

_Noreturn void platform_unexpected_interrupt(void)
{
    platform_panic(PANIC_UNEXPECTED_INTERRUPT);
}
