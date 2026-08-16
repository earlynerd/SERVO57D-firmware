#ifndef MKS57D_SPI1_H
#define MKS57D_SPI1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mks57d/spi_bus.h"

enum
{
    SPI1_TARGET_CLOCK_HZ = 500000u,
    SPI1_MAX_TRANSFER_BYTES = 8u
};

bool spi1_init(uint32_t peripheral_clock_hz);
spi_status_t spi1_exchange(const uint8_t* transmit,
                           uint8_t* receive,
                           size_t length);
spi_bus_t spi1_bus(void);

#endif
