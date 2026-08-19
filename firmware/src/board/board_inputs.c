#include "mks57d/board_inputs.h"

#include <stdint.h>

#include "mks57d/user_inputs.h"
#include "n32l40x.h"

enum
{
    STEP_PIN = 0u,
    DIRECTION_PIN = 8u,
    KEY_NEXT_PIN = 15u,
    ENABLE_PIN = 7u,
    KEY_ENTER_PIN = 8u,
    KEY_MENU_PIN = 9u,
    M_IN2_PIN = 12u,
    M_IN1_PIN = 13u,
    GPIOA_INPUT_MODE_MASK = (3u << (STEP_PIN * 2u)) |
                            (3u << (DIRECTION_PIN * 2u)) |
                            (3u << (KEY_NEXT_PIN * 2u)),
    GPIOB_INPUT_MODE_MASK = (3u << (ENABLE_PIN * 2u)) |
                            (3u << (KEY_ENTER_PIN * 2u)) |
                            (3u << (KEY_MENU_PIN * 2u)) |
                            (3u << (M_IN2_PIN * 2u)) |
                            (3u << (M_IN1_PIN * 2u)),
    GPIOA_INPUT_PULLUPS = 1u << (KEY_NEXT_PIN * 2u),
    GPIOB_INPUT_PULLUPS = (1u << (KEY_ENTER_PIN * 2u)) |
                          (1u << (KEY_MENU_PIN * 2u)) |
                          (1u << (M_IN2_PIN * 2u)) |
                          (1u << (M_IN1_PIN * 2u))
};

bool board_inputs_init(void)
{
    RCC->APB2PCLKEN |= RCC_APB2PCLKEN_IOPAEN |
                       RCC_APB2PCLKEN_IOPBEN;
    __DSB();

    GPIOA->PMODE &= ~((uint32_t)GPIOA_INPUT_MODE_MASK);
    GPIOA->PUPD = (GPIOA->PUPD & ~((uint32_t)GPIOA_INPUT_MODE_MASK)) |
                  (uint32_t)GPIOA_INPUT_PULLUPS;

    GPIOB->PMODE &= ~((uint32_t)GPIOB_INPUT_MODE_MASK);
    GPIOB->PUPD = (GPIOB->PUPD & ~((uint32_t)GPIOB_INPUT_MODE_MASK)) |
                  (uint32_t)GPIOB_INPUT_PULLUPS;

    return ((GPIOA->PMODE & GPIOA_INPUT_MODE_MASK) == 0u) &&
           ((GPIOA->PUPD & GPIOA_INPUT_MODE_MASK) ==
            GPIOA_INPUT_PULLUPS) &&
           ((GPIOB->PMODE & GPIOB_INPUT_MODE_MASK) == 0u) &&
           ((GPIOB->PUPD & GPIOB_INPUT_MODE_MASK) ==
            GPIOB_INPUT_PULLUPS);
}

uint32_t board_inputs_read_raw(void)
{
    const uint32_t gpioa = GPIOA->PID;
    const uint32_t gpiob = GPIOB->PID;
    uint32_t levels = 0u;

    if ((gpioa & (1u << STEP_PIN)) != 0u)
    {
        levels |= USER_INPUT_STEP;
    }
    if ((gpioa & (1u << DIRECTION_PIN)) != 0u)
    {
        levels |= USER_INPUT_DIRECTION;
    }
    if ((gpiob & (1u << ENABLE_PIN)) != 0u)
    {
        levels |= USER_INPUT_ENABLE;
    }
    if ((gpiob & (1u << KEY_ENTER_PIN)) != 0u)
    {
        levels |= USER_INPUT_KEY_ENTER;
    }
    if ((gpiob & (1u << KEY_MENU_PIN)) != 0u)
    {
        levels |= USER_INPUT_KEY_MENU;
    }
    if ((gpioa & (1u << KEY_NEXT_PIN)) != 0u)
    {
        levels |= USER_INPUT_KEY_NEXT;
    }
    if ((gpiob & (1u << M_IN1_PIN)) != 0u)
    {
        levels |= USER_INPUT_M_IN1;
    }
    if ((gpiob & (1u << M_IN2_PIN)) != 0u)
    {
        levels |= USER_INPUT_M_IN2;
    }

    return levels;
}
