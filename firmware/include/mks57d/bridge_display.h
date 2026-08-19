#ifndef MKS57D_BRIDGE_DISPLAY_H
#define MKS57D_BRIDGE_DISPLAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mks57d/bridge_characterizer.h"

enum
{
    BRIDGE_DISPLAY_WIDTH = 72u,
    BRIDGE_DISPLAY_START_PAGE = 1u,
    BRIDGE_DISPLAY_PAGE_COUNT = 2u,
    BRIDGE_DISPLAY_FRAME_BYTES =
        BRIDGE_DISPLAY_WIDTH * BRIDGE_DISPLAY_PAGE_COUNT
};

bool bridge_display_render(uint8_t* pixels,
                           size_t length,
                           bridge_characterizer_leg_t selected_leg,
                           bool active);

#endif
