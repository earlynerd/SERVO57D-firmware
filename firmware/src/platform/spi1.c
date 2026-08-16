#include "mks57d/spi1.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "n32l40x.h"

enum
{
    SPI_GPIO_SCK_PIN = 3u,
    SPI_GPIO_MISO_PIN = 4u,
    SPI_GPIO_MOSI_PIN = 5u,
    SPI_GPIO_CS_PIN = 6u,
    SPI_GPIO_SCK_MASK = 1u << SPI_GPIO_SCK_PIN,
    SPI_GPIO_MISO_MASK = 1u << SPI_GPIO_MISO_PIN,
    SPI_GPIO_MOSI_MASK = 1u << SPI_GPIO_MOSI_PIN,
    SPI_GPIO_CS_MASK = 1u << SPI_GPIO_CS_PIN,
    SPI_GPIO_OUTPUT_MASK = SPI_GPIO_SCK_MASK |
                           SPI_GPIO_MOSI_MASK |
                           SPI_GPIO_CS_MASK,
    SPI_GPIO_MODE_MASK = (3u << (SPI_GPIO_SCK_PIN * 2u)) |
                         (3u << (SPI_GPIO_MISO_PIN * 2u)) |
                         (3u << (SPI_GPIO_MOSI_PIN * 2u)) |
                         (3u << (SPI_GPIO_CS_PIN * 2u)),
    SPI_GPIO_MODE_VALUE = (2u << (SPI_GPIO_SCK_PIN * 2u)) |
                          (2u << (SPI_GPIO_MOSI_PIN * 2u)) |
                          (1u << (SPI_GPIO_CS_PIN * 2u)),
    SPI_GPIO_DRIVE_4MA = (2u << (SPI_GPIO_SCK_PIN * 2u)) |
                         (2u << (SPI_GPIO_MOSI_PIN * 2u)) |
                         (2u << (SPI_GPIO_CS_PIN * 2u)),
    SPI_GPIO_AF_MASK = (15u << (SPI_GPIO_SCK_PIN * 4u)) |
                       (15u << (SPI_GPIO_MISO_PIN * 4u)) |
                       (15u << (SPI_GPIO_MOSI_PIN * 4u)),
    SPI_GPIO_AF_VALUE = (1u << (SPI_GPIO_SCK_PIN * 4u)) |
                        (1u << (SPI_GPIO_MISO_PIN * 4u)),
    SPI_POLL_BUDGET = 8192u,
    SPI_CHIP_SELECT_GUARD_CYCLES = 64u,
    SPI_SUPPORTED_CLOCK_MIN_HZ = 1000000u,
    SPI_SUPPORTED_CLOCK_MAX_HZ = 64000000u,
    SPI_ERROR_MASK = SPI_STS_MODERR | SPI_STS_OVER
};

static bool s_spi1_initialized;
static bool s_spi1_transfer_active;

static void chip_select_guard_delay(void)
{
    uint32_t remaining = SPI_CHIP_SELECT_GUARD_CYCLES;

    /* At the N32L406 64 MHz maximum, 64 NOPs alone cover 1 us. The loop
       overhead only lengthens both the MT6816 CS setup and hold intervals. */
    while (remaining != 0u)
    {
        __NOP();
        --remaining;
    }
}

static uint16_t baud_rate_bits(uint32_t peripheral_clock_hz)
{
    uint32_t divisor = 2u;
    uint16_t bits = 0u;

    while ((peripheral_clock_hz > (SPI1_TARGET_CLOCK_HZ * divisor)) &&
           (divisor < 256u))
    {
        divisor <<= 1u;
        bits = (uint16_t)(bits + SPI_CTRL1_BR0);
    }

    return bits;
}

static spi_status_t status_error(void)
{
    if ((SPI1->STS & SPI_ERROR_MASK) != 0u)
    {
        return SPI_STATUS_PERIPHERAL_ERROR;
    }
    return SPI_STATUS_OK;
}

static spi_status_t wait_for_set(uint16_t mask, spi_status_t timeout_status)
{
    uint32_t remaining = SPI_POLL_BUDGET;

    while (remaining != 0u)
    {
        const spi_status_t error = status_error();

        if (error != SPI_STATUS_OK)
        {
            return error;
        }
        if ((SPI1->STS & mask) == mask)
        {
            return SPI_STATUS_OK;
        }
        --remaining;
    }

    return timeout_status;
}

static spi_status_t wait_for_clear(uint16_t mask,
                                   spi_status_t timeout_status)
{
    uint32_t remaining = SPI_POLL_BUDGET;

    while (remaining != 0u)
    {
        const spi_status_t error = status_error();

        if (error != SPI_STATUS_OK)
        {
            return error;
        }
        if ((SPI1->STS & mask) == 0u)
        {
            return SPI_STATUS_OK;
        }
        --remaining;
    }

    return timeout_status;
}

static void clear_receive_and_overrun(void)
{
    uint32_t remaining = SPI_POLL_BUDGET;

    while (((SPI1->STS & SPI_STS_RNE) != 0u) && (remaining != 0u))
    {
        (void)SPI1->DAT;
        --remaining;
    }
    if ((SPI1->STS & SPI_STS_OVER) != 0u)
    {
        (void)SPI1->DAT;
        (void)SPI1->STS;
    }
}

static void finish_transfer(void)
{
    chip_select_guard_delay();
    GPIOB->PBSC = (uint32_t)SPI_GPIO_CS_MASK;
    s_spi1_transfer_active = false;
}

bool spi1_init(uint32_t peripheral_clock_hz)
{
    uint16_t control;

    s_spi1_initialized = false;
    s_spi1_transfer_active = false;

    if ((peripheral_clock_hz < SPI_SUPPORTED_CLOCK_MIN_HZ) ||
        (peripheral_clock_hz > SPI_SUPPORTED_CLOCK_MAX_HZ))
    {
        return false;
    }

    RCC->APB2PCLKEN |= RCC_APB2PCLKEN_AFIOEN |
                       RCC_APB2PCLKEN_IOPBEN |
                       RCC_APB2PCLKEN_SPI1EN;
    __DSB();

    GPIOB->POD |= (uint32_t)SPI_GPIO_CS_MASK;
    GPIOB->POTYPE &= ~((uint32_t)SPI_GPIO_OUTPUT_MASK);
    GPIOB->PUPD &= ~((uint32_t)SPI_GPIO_MODE_MASK);
    GPIOB->DS = (GPIOB->DS & ~((uint32_t)SPI_GPIO_MODE_MASK)) |
                (uint32_t)SPI_GPIO_DRIVE_4MA;
    *((volatile uint16_t*)&GPIOB->SR) |= (uint16_t)SPI_GPIO_OUTPUT_MASK;
    /* Exact-board Nations mapping: PB3/PB4 use AF1 while PB5 uses AF0. */
    GPIOB->AFL = (GPIOB->AFL & ~((uint32_t)SPI_GPIO_AF_MASK)) |
                 (uint32_t)SPI_GPIO_AF_VALUE;
    GPIOB->PMODE = (GPIOB->PMODE & ~((uint32_t)SPI_GPIO_MODE_MASK)) |
                   (uint32_t)SPI_GPIO_MODE_VALUE;

    RCC->APB2PRST |= RCC_APB2PRST_SPI1RST;
    RCC->APB2PRST &= ~RCC_APB2PRST_SPI1RST;

    SPI1->CTRL1 = 0u;
    SPI1->CTRL2 = 0u;
    SPI1->I2SCFG &= (uint16_t)~SPI_I2SCFG_MODSEL;
    SPI1->CRCPOLY = 7u;

    control = SPI_CTRL1_CLKPOL |
              SPI_CTRL1_CLKPHA |
              SPI_CTRL1_MSEL |
              SPI_CTRL1_SSEL |
              SPI_CTRL1_SSMEN |
              baud_rate_bits(peripheral_clock_hz);
    SPI1->CTRL1 = control;
    SPI1->CTRL1 = control | SPI_CTRL1_SPIEN;

    clear_receive_and_overrun();
    if ((SPI1->STS & SPI_STS_MODERR) != 0u)
    {
        return false;
    }

    s_spi1_initialized = true;
    return true;
}

spi_status_t spi1_exchange(const uint8_t* transmit,
                           uint8_t* receive,
                           size_t length)
{
    spi_status_t result;
    size_t index;

    if ((transmit == NULL) || (receive == NULL) ||
        (length == 0u) || (length > SPI1_MAX_TRANSFER_BYTES))
    {
        return SPI_STATUS_INVALID_ARGUMENT;
    }
    if (!s_spi1_initialized)
    {
        return SPI_STATUS_NOT_READY;
    }
    if (s_spi1_transfer_active || ((SPI1->STS & SPI_STS_BUSY) != 0u))
    {
        return SPI_STATUS_BUS_BUSY;
    }

    clear_receive_and_overrun();
    if (status_error() != SPI_STATUS_OK)
    {
        return SPI_STATUS_PERIPHERAL_ERROR;
    }

    s_spi1_transfer_active = true;
    GPIOB->PBC = (uint32_t)SPI_GPIO_CS_MASK;
    chip_select_guard_delay();

    for (index = 0u; index < length; ++index)
    {
        result = wait_for_set(SPI_STS_TE, SPI_STATUS_TRANSMIT_TIMEOUT);
        if (result != SPI_STATUS_OK)
        {
            finish_transfer();
            return result;
        }

        SPI1->DAT = transmit[index];
        result = wait_for_set(SPI_STS_RNE, SPI_STATUS_RECEIVE_TIMEOUT);
        if (result != SPI_STATUS_OK)
        {
            (void)wait_for_clear(SPI_STS_BUSY,
                                 SPI_STATUS_COMPLETE_TIMEOUT);
            finish_transfer();
            return result;
        }
        receive[index] = (uint8_t)SPI1->DAT;
    }

    result = wait_for_set(SPI_STS_TE, SPI_STATUS_TRANSMIT_TIMEOUT);
    if (result == SPI_STATUS_OK)
    {
        result = wait_for_clear(SPI_STS_BUSY,
                                SPI_STATUS_COMPLETE_TIMEOUT);
    }
    finish_transfer();
    return result;
}

static spi_status_t spi1_bus_exchange(void* context,
                                      const uint8_t* transmit,
                                      uint8_t* receive,
                                      size_t length)
{
    (void)context;
    return spi1_exchange(transmit, receive, length);
}

spi_bus_t spi1_bus(void)
{
    const spi_bus_t bus = {
        .exchange = spi1_bus_exchange,
        .context = NULL,
    };

    return bus;
}
