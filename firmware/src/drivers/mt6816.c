#include "mks57d/mt6816.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum
{
    MT6816_ANGLE_LOW_SHIFT = 2u,
    MT6816_NO_MAGNET_BIT = 1u << 1,
    MT6816_OVER_SPEED_BIT = 1u << 3
};

static bool parity_is_even(uint8_t register_03, uint8_t register_04)
{
    uint16_t value = ((uint16_t)register_03 << 8u) | register_04;
    unsigned int parity = 0u;

    while (value != 0u)
    {
        parity ^= (unsigned int)(value & 1u);
        value >>= 1u;
    }

    return parity == 0u;
}

mt6816_status_t mt6816_decode_registers(uint8_t register_03,
                                        uint8_t register_04,
                                        uint8_t register_05,
                                        mt6816_sample_t* sample)
{
    mt6816_sample_t candidate;

    if (sample == NULL)
    {
        return MT6816_STATUS_INVALID_ARGUMENT;
    }
    if (!parity_is_even(register_03, register_04))
    {
        return MT6816_STATUS_PARITY_ERROR;
    }

    candidate.angle_raw = (uint16_t)(((uint16_t)register_03 << 6u) |
                                     (register_04 >> MT6816_ANGLE_LOW_SHIFT));
    candidate.flags = 0u;
    if ((register_04 & MT6816_NO_MAGNET_BIT) != 0u)
    {
        candidate.flags |= MT6816_FLAG_NO_MAGNET;
    }
    if ((register_05 & MT6816_OVER_SPEED_BIT) != 0u)
    {
        candidate.flags |= MT6816_FLAG_OVER_SPEED;
    }
    candidate.register_03 = register_03;
    candidate.register_04 = register_04;
    candidate.register_05 = register_05;

    *sample = candidate;
    return MT6816_STATUS_OK;
}
