#include "mks57d/i2c1.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "n32l40x.h"

enum
{
    I2C_GPIO_SCL_PIN = 4u,
    I2C_GPIO_SDA_PIN = 5u,
    I2C_GPIO_PIN_MASK = (1u << I2C_GPIO_SCL_PIN) |
                        (1u << I2C_GPIO_SDA_PIN),
    I2C_GPIO_MODE_MASK = (3u << (I2C_GPIO_SCL_PIN * 2u)) |
                         (3u << (I2C_GPIO_SDA_PIN * 2u)),
    I2C_GPIO_MODE_AF = (2u << (I2C_GPIO_SCL_PIN * 2u)) |
                       (2u << (I2C_GPIO_SDA_PIN * 2u)),
    I2C_GPIO_DRIVE_4MA = (2u << (I2C_GPIO_SCL_PIN * 2u)) |
                         (2u << (I2C_GPIO_SDA_PIN * 2u)),
    I2C_GPIO_AF_MASK = (15u << (I2C_GPIO_SCL_PIN * 4u)) |
                       (15u << (I2C_GPIO_SDA_PIN * 4u)),
    I2C_GPIO_AF7 = (7u << (I2C_GPIO_SCL_PIN * 4u)) |
                   (7u << (I2C_GPIO_SDA_PIN * 4u)),
    I2C_OWN_ADDRESS_7BIT_MODE = 0x4000u,
    I2C_FAST_MODE_DUTY_DIVISOR = 3u,
    I2C_FAST_MODE_RISE_TIME_NS = 300u,
    NANOSECONDS_PER_MICROSECOND = 1000u,
    I2C_ERROR_MASK = I2C_STS1_BUSERR | I2C_STS1_ARLOST |
                     I2C_STS1_ACKFAIL | I2C_STS1_OVERRUN |
                     I2C_STS1_TIMOUT,
    I2C_POLL_BUDGET = 100000u
};

static bool s_i2c1_initialized;

static i2c_status_t status_from_error(uint16_t status,
                                      i2c_status_t nack_status)
{
    if ((status & I2C_STS1_ACKFAIL) != 0u)
    {
        return nack_status;
    }
    if ((status & I2C_STS1_ARLOST) != 0u)
    {
        return I2C_STATUS_ARBITRATION_LOST;
    }
    if ((status & I2C_STS1_TIMOUT) != 0u)
    {
        return I2C_STATUS_TIMEOUT;
    }
    return I2C_STATUS_BUS_ERROR;
}

static i2c_status_t wait_for_status(uint16_t required,
                                    i2c_status_t nack_status)
{
    uint32_t remaining = I2C_POLL_BUDGET;

    while (remaining != 0u)
    {
        const uint16_t status = I2C1->STS1;

        if ((status & I2C_ERROR_MASK) != 0u)
        {
            return status_from_error(status, nack_status);
        }
        if ((status & required) == required)
        {
            return I2C_STATUS_OK;
        }
        --remaining;
    }

    return I2C_STATUS_TIMEOUT;
}

static i2c_status_t wait_for_idle(void)
{
    uint32_t remaining = I2C_POLL_BUDGET;

    while (remaining != 0u)
    {
        if ((I2C1->STS2 & I2C_STS2_BUSY) == 0u)
        {
            return I2C_STATUS_OK;
        }
        --remaining;
    }

    return I2C_STATUS_BUS_BUSY;
}

static void abort_transfer(void)
{
    I2C1->CTRL1 |= I2C_CTRL1_STOPGEN;
    I2C1->STS1 = (uint16_t)~((uint16_t)I2C_ERROR_MASK);
}

bool i2c1_init(uint32_t peripheral_clock_hz)
{
    uint32_t frequency_mhz;
    uint32_t clock_control;
    uint32_t actual_clock_hz;
    const uint32_t target_denominator =
        I2C_FAST_MODE_DUTY_DIVISOR * I2C1_TARGET_CLOCK_HZ;

    s_i2c1_initialized = false;

    if ((peripheral_clock_hz < 2000000u) ||
        (peripheral_clock_hz > 36000000u) ||
        ((peripheral_clock_hz % 1000000u) != 0u))
    {
        return false;
    }

    frequency_mhz = peripheral_clock_hz / 1000000u;
    /* Choose the closest divider to the bench-proven 333.3 kHz rate. Both
       the old 4 MHz PCLK and the production 16 MHz PCLK divide exactly
       enough for CCR=4 and CCR=16 respectively. */
    clock_control =
        (peripheral_clock_hz + (target_denominator / 2u)) /
        target_denominator;
    if (clock_control == 0u)
    {
        clock_control = 1u;
    }
    if (clock_control > I2C_CLKCTRL_CLKCTRL)
    {
        return false;
    }
    actual_clock_hz = peripheral_clock_hz /
                      (I2C_FAST_MODE_DUTY_DIVISOR * clock_control);
    if (actual_clock_hz > I2C1_FAST_MODE_LIMIT_HZ)
    {
        return false;
    }

    RCC->APB2PCLKEN |= RCC_APB2PCLKEN_AFIOEN |
                       RCC_APB2PCLKEN_IOPAEN;
    RCC->APB1PCLKEN |= RCC_APB1PCLKEN_I2C1EN;
    __DSB();

    GPIOA->POD |= (uint32_t)I2C_GPIO_PIN_MASK;
    GPIOA->POTYPE |= (uint32_t)I2C_GPIO_PIN_MASK;
    GPIOA->PUPD &= ~((uint32_t)I2C_GPIO_MODE_MASK);
    GPIOA->DS = (GPIOA->DS & ~((uint32_t)I2C_GPIO_MODE_MASK)) |
                (uint32_t)I2C_GPIO_DRIVE_4MA;
    *((volatile uint16_t*)&GPIOA->SR) |= (uint16_t)I2C_GPIO_PIN_MASK;
    GPIOA->AFL = (GPIOA->AFL & ~((uint32_t)I2C_GPIO_AF_MASK)) |
                 (uint32_t)I2C_GPIO_AF7;
    GPIOA->PMODE = (GPIOA->PMODE & ~((uint32_t)I2C_GPIO_MODE_MASK)) |
                   (uint32_t)I2C_GPIO_MODE_AF;

    RCC->APB1PRST |= RCC_APB1PRST_I2C1RST;
    RCC->APB1PRST &= ~RCC_APB1PRST_I2C1RST;

    I2C1->CTRL1 = 0u;
    I2C1->CTRL2 = (uint16_t)frequency_mhz;
    I2C1->CLKCTRL = (uint16_t)(I2C_CLKCTRL_FSMODE | clock_control);
    I2C1->TMRISE = (uint16_t)(
        ((frequency_mhz * I2C_FAST_MODE_RISE_TIME_NS) /
         NANOSECONDS_PER_MICROSECOND) + 1u);
    I2C1->OADDR1 = (uint16_t)I2C_OWN_ADDRESS_7BIT_MODE;
    I2C1->CTRL1 = I2C_CTRL1_EN;

    s_i2c1_initialized = true;
    return true;
}

i2c_status_t i2c1_write(uint8_t address_7bit,
                        const uint8_t* bytes,
                        size_t length)
{
    i2c_status_t result;
    size_t index;

    if ((address_7bit > 0x7Fu) ||
        ((bytes == NULL) && (length != 0u)) ||
        (length > I2C1_MAX_TRANSFER_BYTES))
    {
        return I2C_STATUS_INVALID_ARGUMENT;
    }
    if (!s_i2c1_initialized)
    {
        return I2C_STATUS_NOT_READY;
    }

    result = wait_for_idle();
    if (result != I2C_STATUS_OK)
    {
        return result;
    }

    I2C1->CTRL1 |= I2C_CTRL1_STARTGEN;
    result = wait_for_status(I2C_STS1_STARTBF,
                             I2C_STATUS_ADDRESS_NACK);
    if (result != I2C_STATUS_OK)
    {
        abort_transfer();
        return result;
    }

    I2C1->DAT = (uint16_t)((uint16_t)address_7bit << 1u);
    result = wait_for_status(I2C_STS1_ADDRF,
                             I2C_STATUS_ADDRESS_NACK);
    if (result != I2C_STATUS_OK)
    {
        abort_transfer();
        return result;
    }

    /* Reading STS1 followed by STS2 acknowledges the address phase. */
    (void)I2C1->STS1;
    (void)I2C1->STS2;

    for (index = 0u; index < length; ++index)
    {
        result = wait_for_status(I2C_STS1_TXDATE,
                                 I2C_STATUS_DATA_NACK);
        if (result != I2C_STATUS_OK)
        {
            abort_transfer();
            return result;
        }
        I2C1->DAT = bytes[index];
    }

    if (length != 0u)
    {
        result = wait_for_status(I2C_STS1_BSF,
                                 I2C_STATUS_DATA_NACK);
        if (result != I2C_STATUS_OK)
        {
            abort_transfer();
            return result;
        }
    }

    I2C1->CTRL1 |= I2C_CTRL1_STOPGEN;
    return I2C_STATUS_OK;
}

static i2c_status_t i2c1_bus_write(void* context,
                                   uint8_t address_7bit,
                                   const uint8_t* bytes,
                                   size_t length)
{
    (void)context;
    return i2c1_write(address_7bit, bytes, length);
}

i2c_bus_t i2c1_bus(void)
{
    const i2c_bus_t bus = {
        .write = i2c1_bus_write,
        .context = NULL,
    };

    return bus;
}
