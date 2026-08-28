#ifndef MKS57D_ROTOR_DISPLAY_H
#define MKS57D_ROTOR_DISPLAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum
{
    ROTOR_DISPLAY_WIDTH = 72u,
    ROTOR_DISPLAY_START_PAGE = 1u,
    ROTOR_DISPLAY_PAGE_COUNT = 2u,
    ROTOR_DISPLAY_FRAME_BYTES =
        ROTOR_DISPLAY_WIDTH * ROTOR_DISPLAY_PAGE_COUNT
};

/* Render boot-session unwrapped mechanical position in revolutions and
 * filtered mechanical velocity in revolutions per second. The initial position
 * retains the encoder's within-turn fraction, while later wraps accumulate
 * until reset. Each row uses a signed fixed-point value with three decimal
 * places: P+123.456 / V-012.345. Invalid or out-of-range values retain the row
 * label and show dashes. */
bool rotor_display_render(uint8_t* pixels,
                          size_t length,
                          float position_revolutions,
                          float velocity_revolutions_per_second,
                          bool valid);

#endif
