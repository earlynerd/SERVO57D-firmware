#ifndef MKS57D_INPUT_DISPLAY_H
#define MKS57D_INPUT_DISPLAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum
{
    INPUT_DISPLAY_WIDTH = 72u,
    INPUT_DISPLAY_START_PAGE = 1u,
    INPUT_DISPLAY_PAGE_COUNT = 2u,
    INPUT_DISPLAY_FRAME_BYTES =
        INPUT_DISPLAY_WIDTH * INPUT_DISPLAY_PAGE_COUNT
};

/* Draw E/M/N/1/2 labels above their debounced raw electrical levels. */
bool input_display_render(uint8_t* pixels,
                          size_t length,
                          uint32_t raw_levels,
                          bool valid);

#endif
