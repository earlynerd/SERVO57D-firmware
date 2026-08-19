#ifndef MKS57D_ENCODER_DISPLAY_H
#define MKS57D_ENCODER_DISPLAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum
{
    ENCODER_DISPLAY_WIDTH = 72u,
    ENCODER_DISPLAY_START_PAGE = 1u,
    ENCODER_DISPLAY_PAGE_COUNT = 2u,
    ENCODER_DISPLAY_FRAME_BYTES =
        ENCODER_DISPLAY_WIDTH * ENCODER_DISPLAY_PAGE_COUNT
};

/* Render a five-digit, zero-padded 14-bit raw angle. Invalid feedback is
   represented by five dashes. The result occupies two SSD1306 pages. */
bool encoder_display_render(uint8_t* pixels,
                            size_t length,
                            uint16_t angle_raw,
                            bool valid);

#endif
