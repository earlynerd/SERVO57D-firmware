#ifndef MKS57D_I2C_BUS_H
#define MKS57D_I2C_BUS_H

#include <stddef.h>
#include <stdint.h>

typedef enum
{
    I2C_STATUS_OK = 0,
    I2C_STATUS_INVALID_ARGUMENT,
    I2C_STATUS_NOT_READY,
    I2C_STATUS_BUS_BUSY,
    I2C_STATUS_ADDRESS_NACK,
    I2C_STATUS_DATA_NACK,
    I2C_STATUS_ARBITRATION_LOST,
    I2C_STATUS_BUS_ERROR,
    I2C_STATUS_TIMEOUT
} i2c_status_t;

typedef i2c_status_t (*i2c_write_fn)(void* context,
                                     uint8_t address_7bit,
                                     const uint8_t* bytes,
                                     size_t length);

typedef struct
{
    i2c_write_fn write;
    void* context;
} i2c_bus_t;

#endif
