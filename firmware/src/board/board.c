#include "mks57d/board.h"

#include <stdint.h>

#include "mks57d/tim3_bridge_pwm.h"
#include "n32l40x.h"

enum
{
    STATUS_LED_PIN = 0u,
    STATUS_LED_MASK = 1u << STATUS_LED_PIN,
    STATUS_LED_MODE_SHIFT = STATUS_LED_PIN * 2u,
    STATUS_LED_MODE_MASK = 3u << STATUS_LED_MODE_SHIFT,
    STATUS_LED_OUTPUT_MODE = 1u << STATUS_LED_MODE_SHIFT,
    STATUS_LED_4MA_DRIVE = 2u << STATUS_LED_MODE_SHIFT,
    DISPLAY_RESET_PIN = 2u,
    DISPLAY_RESET_MASK = 1u << DISPLAY_RESET_PIN,
    DISPLAY_RESET_MODE_SHIFT = DISPLAY_RESET_PIN * 2u,
    DISPLAY_RESET_MODE_MASK = 3u << DISPLAY_RESET_MODE_SHIFT,
    DISPLAY_RESET_OUTPUT_MODE = 1u << DISPLAY_RESET_MODE_SHIFT,
    DISPLAY_RESET_4MA_DRIVE = 2u << DISPLAY_RESET_MODE_SHIFT,
    GPIO_MODE_FIELD_LSB_MASK = 0x55555555u,
    BRIDGE_PA_PIN_MASK = (1u << 6u) | (1u << 7u),
    BRIDGE_PB_PIN_MASK = (1u << 0u) | (1u << 1u),
    BRIDGE_PA_MODE_MASK = (3u << (6u * 2u)) |
                          (3u << (7u * 2u)),
    BRIDGE_PB_CONTROL_MODE_MASK = (3u << (0u * 2u)) |
                                  (3u << (1u * 2u)),
    BRIDGE_PB_PASSIVE_MODE_MASK = BRIDGE_PB_CONTROL_MODE_MASK |
                                  (3u << (7u * 2u)),
    BRIDGE_PB_OUTPUT_MODE_MASK = (1u << (0u * 2u)) |
                                 (1u << (1u * 2u)),
    BRIDGE_PA_OUTPUT_MODE_MASK = (1u << (6u * 2u)) |
                                 (1u << (7u * 2u)),
    BRIDGE_PB_AF_MODE_MASK = (2u << (0u * 2u)) |
                             (2u << (1u * 2u)),
    BRIDGE_PA_AF_MODE_MASK = (2u << (6u * 2u)) |
                             (2u << (7u * 2u)),
    BRIDGE_PB_DRIVE_4MA_MASK = (2u << (0u * 2u)) |
                               (2u << (1u * 2u)),
    BRIDGE_PA_DRIVE_4MA_MASK = (2u << (6u * 2u)) |
                               (2u << (7u * 2u)),
    BRIDGE_GPIO_AF_TIM3 = 2u,
    BRIDGE_PA_AF_MASK = (0xFu << (6u * 4u)) |
                        (0xFu << (7u * 4u)),
    BRIDGE_PB_AF_MASK = (0xFu << (0u * 4u)) |
                        (0xFu << (1u * 4u)),
    BRIDGE_PA_AF_TIM3_MASK = (BRIDGE_GPIO_AF_TIM3 << (6u * 4u)) |
                             (BRIDGE_GPIO_AF_TIM3 << (7u * 4u)),
    BRIDGE_PB_AF_TIM3_MASK = (BRIDGE_GPIO_AF_TIM3 << (0u * 4u)) |
                             (BRIDGE_GPIO_AF_TIM3 << (1u * 4u))
};

static bool gpio_modes_are_non_driving(uint32_t modes, uint32_t mask)
{
    /* PMODE 00 is digital input and 11 is analog. Both are high impedance.
       Output 01 and alternate-function 10 have unequal field bits, so this
       rejects every actively driven bridge-pin mode without requiring one
       particular high-impedance state. */
    const uint32_t selected_mode_lsbs =
        (uint32_t)mask & (uint32_t)GPIO_MODE_FIELD_LSB_MASK;

    return (((modes ^ (modes >> 1u)) & selected_mode_lsbs) == 0u);
}

void board_init_passive(void)
{
    /* The RS-485 V1.1 schematic drives its blue status LED from PD0. */
    RCC->APB2PCLKEN |= RCC_APB2PCLKEN_IOPBEN |
                       RCC_APB2PCLKEN_IOPDEN;
    __DSB();

    /* PB2 has no external bias. Preload the active-low display reset before
       making it an output so the panel starts from a defined state. */
    GPIOB->PBC = (uint32_t)DISPLAY_RESET_MASK;
    GPIOB->POTYPE &= ~((uint32_t)DISPLAY_RESET_MASK);
    GPIOB->PUPD &= ~((uint32_t)DISPLAY_RESET_MODE_MASK);
    GPIOB->DS = (GPIOB->DS & ~((uint32_t)DISPLAY_RESET_MODE_MASK)) |
                (uint32_t)DISPLAY_RESET_4MA_DRIVE;
    *((volatile uint16_t*)&GPIOB->SR) |= (uint16_t)DISPLAY_RESET_MASK;
    GPIOB->PMODE =
        (GPIOB->PMODE & ~((uint32_t)DISPLAY_RESET_MODE_MASK)) |
        (uint32_t)DISPLAY_RESET_OUTPUT_MODE;

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

    /* GPIOB is active only to hold the low-energy display in reset. */
    return ((port_clocks & RCC_APB2PCLKEN_IOPAEN) == 0u) &&
           ((port_clocks & RCC_APB2PCLKEN_IOPBEN) != 0u) &&
           ((port_clocks & RCC_APB2PCLKEN_IOPDEN) != 0u) &&
           ((GPIOB->PMODE & DISPLAY_RESET_MODE_MASK) ==
            DISPLAY_RESET_OUTPUT_MODE) &&
           ((GPIOB->POD & DISPLAY_RESET_MASK) == 0u) &&
           gpio_modes_are_non_driving(GPIOB->PMODE,
                                       BRIDGE_PB_PASSIVE_MODE_MASK) &&
           ((GPIOB->PUPD & BRIDGE_PB_PASSIVE_MODE_MASK) == 0u) &&
           ((GPIOD->PMODE & STATUS_LED_MODE_MASK) ==
            STATUS_LED_OUTPUT_MODE);
}

void board_bridge_force_low_zero(void)
{
    RCC->APB2PCLKEN |= RCC_APB2PCLKEN_IOPAEN |
                       RCC_APB2PCLKEN_IOPBEN;
    __DSB();

    /* HIN is active-high and LIN is active-low. Each leg command is wired to
       both inputs, so a low command selects the low-side FET. Preload all four
       commands low before changing their modes. */
    GPIOA->PBC = (uint32_t)BRIDGE_PA_PIN_MASK;
    GPIOB->PBC = (uint32_t)BRIDGE_PB_PIN_MASK;

    GPIOA->POTYPE &= ~((uint32_t)BRIDGE_PA_PIN_MASK);
    GPIOB->POTYPE &= ~((uint32_t)BRIDGE_PB_PIN_MASK);
    GPIOA->PUPD &= ~((uint32_t)BRIDGE_PA_MODE_MASK);
    GPIOB->PUPD &= ~((uint32_t)BRIDGE_PB_CONTROL_MODE_MASK);
    GPIOA->DS = (GPIOA->DS & ~((uint32_t)BRIDGE_PA_MODE_MASK)) |
                (uint32_t)BRIDGE_PA_DRIVE_4MA_MASK;
    GPIOB->DS = (GPIOB->DS &
                 ~((uint32_t)BRIDGE_PB_CONTROL_MODE_MASK)) |
                (uint32_t)BRIDGE_PB_DRIVE_4MA_MASK;
    *((volatile uint16_t*)&GPIOA->SR) |=
        (uint16_t)BRIDGE_PA_PIN_MASK;
    *((volatile uint16_t*)&GPIOB->SR) |=
        (uint16_t)BRIDGE_PB_PIN_MASK;

    GPIOA->PMODE =
        (GPIOA->PMODE & ~((uint32_t)BRIDGE_PA_MODE_MASK)) |
        (uint32_t)BRIDGE_PA_OUTPUT_MODE_MASK;
    GPIOB->PMODE =
        (GPIOB->PMODE &
         ~((uint32_t)BRIDGE_PB_CONTROL_MODE_MASK)) |
        (uint32_t)BRIDGE_PB_OUTPUT_MODE_MASK;
    __DSB();

    tim3_bridge_pwm_stop();
}

bool board_bridge_pwm_init(uint32_t timer_clock_hz)
{
    board_bridge_force_low_zero();

    if (!tim3_bridge_pwm_init(timer_clock_hz))
    {
        return false;
    }

    GPIOA->AFL = (GPIOA->AFL & ~((uint32_t)BRIDGE_PA_AF_MASK)) |
                 (uint32_t)BRIDGE_PA_AF_TIM3_MASK;
    GPIOB->AFL = (GPIOB->AFL & ~((uint32_t)BRIDGE_PB_AF_MASK)) |
                 (uint32_t)BRIDGE_PB_AF_TIM3_MASK;
    GPIOA->PMODE =
        (GPIOA->PMODE & ~((uint32_t)BRIDGE_PA_MODE_MASK)) |
        (uint32_t)BRIDGE_PA_AF_MODE_MASK;
    GPIOB->PMODE =
        (GPIOB->PMODE &
         ~((uint32_t)BRIDGE_PB_CONTROL_MODE_MASK)) |
        (uint32_t)BRIDGE_PB_AF_MODE_MASK;
    __DSB();

    if (((GPIOA->PMODE & BRIDGE_PA_MODE_MASK) !=
         BRIDGE_PA_AF_MODE_MASK) ||
        ((GPIOB->PMODE & BRIDGE_PB_CONTROL_MODE_MASK) !=
         BRIDGE_PB_AF_MODE_MASK) ||
        ((GPIOA->AFL & BRIDGE_PA_AF_MASK) !=
         BRIDGE_PA_AF_TIM3_MASK) ||
        ((GPIOB->AFL & BRIDGE_PB_AF_MASK) !=
         BRIDGE_PB_AF_TIM3_MASK))
    {
        board_bridge_force_low_zero();
        return false;
    }

    return true;
}

void board_display_reset_assert(void)
{
    GPIOB->PBC = (uint32_t)DISPLAY_RESET_MASK;
}

void board_display_reset_release(void)
{
    GPIOB->PBSC = (uint32_t)DISPLAY_RESET_MASK;
}

void board_status_led_toggle(void)
{
    GPIOD->POD ^= (uint32_t)STATUS_LED_MASK;
}
