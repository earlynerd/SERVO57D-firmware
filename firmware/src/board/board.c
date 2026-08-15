#include "mks57d/board.h"

#include <stdint.h>

#include "n32l40x.h"

enum
{
    STATUS_LED_PIN = 9u,
    STATUS_LED_MASK = 1u << STATUS_LED_PIN,
    STATUS_LED_MODE_SHIFT = STATUS_LED_PIN * 2u,
    STATUS_LED_MODE_MASK = 3u << STATUS_LED_MODE_SHIFT,
    STATUS_LED_OUTPUT_MODE = 1u << STATUS_LED_MODE_SHIFT,
    STATUS_LED_4MA_DRIVE = 2u << STATUS_LED_MODE_SHIFT
};

void board_init_passive(void)
{
    /* PB9 is provisional until the purchased PCB revision is inspected. */
    RCC->APB2PCLKEN |= RCC_APB2PCLKEN_IOPBEN;
    __DSB();

    /* Preload a deterministic low level before changing PB9 to an output. */
    GPIOB->POD &= ~((uint32_t)STATUS_LED_MASK);
    GPIOB->POTYPE &= ~((uint32_t)STATUS_LED_MASK);
    GPIOB->PUPD &= ~((uint32_t)STATUS_LED_MODE_MASK);
    GPIOB->DS = (GPIOB->DS & ~((uint32_t)STATUS_LED_MODE_MASK)) |
                (uint32_t)STATUS_LED_4MA_DRIVE;

    /* GPIOx_SR is the one GPIO register the TRM requires to be accessed as
       a 16-bit word. Keep the status LED on the slow slew-rate setting. */
    *((volatile uint16_t*)&GPIOB->SR) |= (uint16_t)STATUS_LED_MASK;

    GPIOB->PMODE = (GPIOB->PMODE & ~((uint32_t)STATUS_LED_MODE_MASK)) |
                   (uint32_t)STATUS_LED_OUTPUT_MODE;
}

void board_status_led_toggle(void)
{
    GPIOB->POD ^= (uint32_t)STATUS_LED_MASK;
}
