#include "mks57d/board.h"

#include <stdint.h>

#include "n32l40x.h"

enum
{
    STATUS_LED_PIN = 0u,
    STATUS_LED_MASK = 1u << STATUS_LED_PIN,
    STATUS_LED_MODE_SHIFT = STATUS_LED_PIN * 2u,
    STATUS_LED_MODE_MASK = 3u << STATUS_LED_MODE_SHIFT,
    STATUS_LED_OUTPUT_MODE = 1u << STATUS_LED_MODE_SHIFT,
    STATUS_LED_4MA_DRIVE = 2u << STATUS_LED_MODE_SHIFT,
    BRIDGE_PA_MODE_MASK = (3u << (6u * 2u)) |
                          (3u << (7u * 2u)),
    BRIDGE_PB_MODE_MASK = (3u << (0u * 2u)) |
                          (3u << (1u * 2u)) |
                          (3u << (7u * 2u)) |
                          (3u << (9u * 2u))
};

void board_init_passive(void)
{
    /* The RS-485 V1.1 schematic drives its blue status LED from PD0. */
    RCC->APB2PCLKEN |= RCC_APB2PCLKEN_IOPDEN;
    __DSB();

    /* Preload LED-off low before changing PD0 to a push-pull output. */
    GPIOD->POD &= ~((uint32_t)STATUS_LED_MASK);
    GPIOD->POTYPE &= ~((uint32_t)STATUS_LED_MASK);
    GPIOD->PUPD &= ~((uint32_t)STATUS_LED_MODE_MASK);
    GPIOD->DS = (GPIOD->DS & ~((uint32_t)STATUS_LED_MODE_MASK)) |
                (uint32_t)STATUS_LED_4MA_DRIVE;

    /* GPIOx_SR is the one GPIO register the TRM requires to be accessed as
       a 16-bit word. Keep the status LED on the slow slew-rate setting. */
    *((volatile uint16_t*)&GPIOD->SR) |= (uint16_t)STATUS_LED_MASK;

    GPIOD->PMODE = (GPIOD->PMODE & ~((uint32_t)STATUS_LED_MODE_MASK)) |
                   (uint32_t)STATUS_LED_OUTPUT_MODE;
}

bool board_passive_invariants_hold(void)
{
    const uint32_t port_clocks = RCC->APB2PCLKEN;

    /* GPIOB remains clock-gated until SPI1 initialization. */
    return ((port_clocks & RCC_APB2PCLKEN_IOPAEN) == 0u) &&
           ((port_clocks & RCC_APB2PCLKEN_IOPBEN) == 0u) &&
           ((port_clocks & RCC_APB2PCLKEN_IOPDEN) != 0u) &&
           ((GPIOD->PMODE & STATUS_LED_MODE_MASK) ==
            STATUS_LED_OUTPUT_MODE);
}

bool board_bridge_invariants_hold(void)
{
    const uint32_t port_clocks = RCC->APB2PCLKEN;

    if (((port_clocks & RCC_APB2PCLKEN_IOPAEN) != 0u) &&
        (((GPIOA->PMODE & BRIDGE_PA_MODE_MASK) != 0u) ||
         ((GPIOA->PUPD & BRIDGE_PA_MODE_MASK) != 0u)))
    {
        return false;
    }
    if ((port_clocks & RCC_APB2PCLKEN_IOPBEN) == 0u)
    {
        return true;
    }

    return ((GPIOB->PMODE & BRIDGE_PB_MODE_MASK) == 0u) &&
           ((GPIOB->PUPD & BRIDGE_PB_MODE_MASK) == 0u);
}

void board_status_led_toggle(void)
{
    GPIOD->POD ^= (uint32_t)STATUS_LED_MASK;
}
