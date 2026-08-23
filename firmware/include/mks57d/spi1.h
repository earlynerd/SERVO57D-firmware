#ifndef MKS57D_SPI1_H
#define MKS57D_SPI1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mks57d/spi_status.h"

enum
{
    SPI1_TARGET_CLOCK_HZ = 500000u,
    SPI1_MAX_TRANSFER_BYTES = 8u,
    SPI1_PERIODIC_FREQUENCY_HZ = 1000u
};

typedef void (*spi1_periodic_exchange_callback_t)(
    void* context,
    spi_status_t status,
    const uint8_t* receive,
    size_t length,
    uint32_t timestamp_us);

bool spi1_init(uint32_t peripheral_clock_hz);
bool spi1_periodic_exchange_start(
    const uint8_t* transmit,
    size_t length,
    uint32_t timer_clock_hz,
    uint32_t initial_delay_millis,
    spi1_periodic_exchange_callback_t callback,
    void* callback_context);
#endif
