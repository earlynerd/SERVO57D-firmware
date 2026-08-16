#ifndef MKS57D_MT6816_H
#define MKS57D_MT6816_H

#include <stdint.h>

#include "mks57d/spi_bus.h"

enum
{
    MT6816_ANGLE_RAW_MAX = 16383u,
    MT6816_FLAG_NO_MAGNET = 1u << 0,
    MT6816_FLAG_OVER_SPEED = 1u << 1
};

typedef enum
{
    MT6816_STATUS_NOT_ATTEMPTED = 0,
    MT6816_STATUS_OK,
    MT6816_STATUS_INVALID_ARGUMENT,
    MT6816_STATUS_TRANSPORT_ERROR,
    MT6816_STATUS_PARITY_ERROR
} mt6816_status_t;

typedef struct
{
    uint16_t angle_raw;
    uint8_t flags;
    uint8_t register_03;
    uint8_t register_04;
    uint8_t register_05;
} mt6816_sample_t;

mt6816_status_t mt6816_decode_registers(uint8_t register_03,
                                        uint8_t register_04,
                                        uint8_t register_05,
                                        mt6816_sample_t* sample);
mt6816_status_t mt6816_read_angle(const spi_bus_t* bus,
                                  mt6816_sample_t* sample,
                                  spi_status_t* transport_status);

#endif
