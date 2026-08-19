#ifndef MKS57D_I2C1_H
#define MKS57D_I2C1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mks57d/i2c_bus.h"

enum
{
    I2C1_TARGET_CLOCK_HZ = 333333u,
    I2C1_FAST_MODE_LIMIT_HZ = 400000u,
    I2C1_MAX_TRANSFER_BYTES = 32u
};

/*
 * Provisional passive-peripheral transport for PA4/PA5. Merely linking this
 * module does not configure the pins; i2c1_init() must be called explicitly.
 */
bool i2c1_init(uint32_t peripheral_clock_hz);
i2c_status_t i2c1_write(uint8_t address_7bit,
                        const uint8_t* bytes,
                        size_t length);
i2c_bus_t i2c1_bus(void);

#endif
