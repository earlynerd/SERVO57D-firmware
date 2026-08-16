#ifndef MKS57D_SPI_BUS_H
#define MKS57D_SPI_BUS_H

#include <stddef.h>
#include <stdint.h>

typedef enum
{
    SPI_STATUS_OK = 0,
    SPI_STATUS_INVALID_ARGUMENT,
    SPI_STATUS_NOT_READY,
    SPI_STATUS_BUS_BUSY,
    SPI_STATUS_TRANSMIT_TIMEOUT,
    SPI_STATUS_RECEIVE_TIMEOUT,
    SPI_STATUS_COMPLETE_TIMEOUT,
    SPI_STATUS_PERIPHERAL_ERROR
} spi_status_t;

typedef spi_status_t (*spi_exchange_fn)(void* context,
                                        const uint8_t* transmit,
                                        uint8_t* receive,
                                        size_t length);

typedef struct
{
    spi_exchange_fn exchange;
    void* context;
} spi_bus_t;

#endif
