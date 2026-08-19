#ifndef MKS57D_PULSE_INPUT_DISPLAY_H
#define MKS57D_PULSE_INPUT_DISPLAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum
{
    PULSE_INPUT_DISPLAY_WIDTH = 72u,
    PULSE_INPUT_DISPLAY_START_PAGE = 1u,
    PULSE_INPUT_DISPLAY_PAGE_COUNT = 2u,
    PULSE_INPUT_DISPLAY_FRAME_BYTES =
        PULSE_INPUT_DISPLAY_WIDTH * PULSE_INPUT_DISPLAY_PAGE_COUNT
};

/* Draw S/D/E labels above their debounced raw electrical levels. */
bool pulse_input_display_render(uint8_t* pixels,
                                size_t length,
                                uint32_t raw_levels,
                                bool valid);

#endif
