#ifndef MKS57D_SPI1_H
#define MKS57D_SPI1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mks57d/spi_bus.h"

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

typedef struct
{
    uint32_t completed_count;
    uint32_t error_count;
    uint32_t overrun_count;
    uint32_t deferred_overrun_count;
    uint32_t latest_interval_us;
    uint32_t maximum_interval_us;
    bool active;
} spi1_periodic_stats_t;

bool spi1_init(uint32_t peripheral_clock_hz);
spi_status_t spi1_exchange(const uint8_t* transmit,
                           uint8_t* receive,
                           size_t length);
spi_bus_t spi1_bus(void);
bool spi1_periodic_exchange_start(
    const uint8_t* transmit,
    size_t length,
    uint32_t timer_clock_hz,
    uint32_t initial_delay_millis,
    spi1_periodic_exchange_callback_t callback,
    void* callback_context);
void spi1_periodic_exchange_stop(void);
void spi1_periodic_exchange_get_stats(spi1_periodic_stats_t* stats);

#endif
