#include "mks57d/panic.h"

#include "mks57d/board.h"
#include "n32l40x.h"

volatile panic_code_t g_last_panic __attribute__((section(".noinit")));

_Noreturn void platform_panic(panic_code_t code)
{
    __disable_irq();
    board_bridge_force_low_zero();
    g_last_panic = code;
    __DSB();

    for (;;)
    {
        __NOP();
    }
}

void NMI_Handler(void)
{
    platform_panic(PANIC_NMI);
}

void HardFault_Handler(void)
{
    platform_panic(PANIC_HARD_FAULT);
}

void MemManage_Handler(void)
{
    platform_panic(PANIC_MEMORY_FAULT);
}

void BusFault_Handler(void)
{
    platform_panic(PANIC_BUS_FAULT);
}

void UsageFault_Handler(void)
{
    platform_panic(PANIC_USAGE_FAULT);
}

_Noreturn void platform_unexpected_interrupt(void)
{
    platform_panic(PANIC_UNEXPECTED_INTERRUPT);
}
