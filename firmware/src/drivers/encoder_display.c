#include "mks57d/encoder_display.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum
{
    GLYPH_WIDTH = 5u,
    GLYPH_HEIGHT = 7u,
    GLYPH_SCALE = 2u,
    GLYPH_SPACING = 2u,
    GLYPH_COUNT = 5u,
    LABEL_WIDTH = (GLYPH_COUNT * GLYPH_WIDTH * GLYPH_SCALE) +
                  ((GLYPH_COUNT - 1u) * GLYPH_SPACING),
    LABEL_X = (ENCODER_DISPLAY_WIDTH - LABEL_WIDTH) / 2u,
    LABEL_Y = 1u,
    INVALID_GLYPH = 10u,
    ENCODER_RAW_MAX = 16383u
};

static const uint8_t s_glyphs[][GLYPH_WIDTH] = {
    {0x3Eu, 0x51u, 0x49u, 0x45u, 0x3Eu}, /* 0 */
    {0x00u, 0x42u, 0x7Fu, 0x40u, 0x00u}, /* 1 */
    {0x42u, 0x61u, 0x51u, 0x49u, 0x46u}, /* 2 */
    {0x21u, 0x41u, 0x45u, 0x4Bu, 0x31u}, /* 3 */
    {0x18u, 0x14u, 0x12u, 0x7Fu, 0x10u}, /* 4 */
    {0x27u, 0x45u, 0x45u, 0x45u, 0x39u}, /* 5 */
    {0x3Cu, 0x4Au, 0x49u, 0x49u, 0x30u}, /* 6 */
    {0x01u, 0x71u, 0x09u, 0x05u, 0x03u}, /* 7 */
    {0x36u, 0x49u, 0x49u, 0x49u, 0x36u}, /* 8 */
    {0x06u, 0x49u, 0x49u, 0x29u, 0x1Eu}, /* 9 */
    {0x08u, 0x08u, 0x08u, 0x08u, 0x08u}, /* - */
};

static void set_pixel(uint8_t* pixels, size_t x, size_t y)
{
    const size_t index = ((y / 8u) * ENCODER_DISPLAY_WIDTH) + x;

    pixels[index] |= (uint8_t)(1u << (y % 8u));
}

static void draw_glyph(uint8_t* pixels,
                       size_t glyph_index,
                       size_t x)
{
    size_t column;

    for (column = 0u; column < GLYPH_WIDTH; ++column)
    {
        size_t row;
        const uint8_t column_bits = s_glyphs[glyph_index][column];

        for (row = 0u; row < GLYPH_HEIGHT; ++row)
        {
            if ((column_bits & (uint8_t)(1u << row)) != 0u)
            {
                size_t scale_x;

                for (scale_x = 0u; scale_x < GLYPH_SCALE; ++scale_x)
                {
                    size_t scale_y;

                    for (scale_y = 0u; scale_y < GLYPH_SCALE; ++scale_y)
                    {
                        set_pixel(pixels,
                                  x + (column * GLYPH_SCALE) + scale_x,
                                  LABEL_Y + (row * GLYPH_SCALE) + scale_y);
                    }
                }
            }
        }
    }
}

bool encoder_display_render(uint8_t* pixels,
                            size_t length,
                            uint16_t angle_raw,
                            bool valid)
{
    uint8_t digits[GLYPH_COUNT];
    size_t glyph;

    if ((pixels == NULL) || (length != ENCODER_DISPLAY_FRAME_BYTES))
    {
        return false;
    }

    memset(pixels, 0, length);
    if (!valid || (angle_raw > ENCODER_RAW_MAX))
    {
        for (glyph = 0u; glyph < GLYPH_COUNT; ++glyph)
        {
            digits[glyph] = INVALID_GLYPH;
        }
    }
    else
    {
        uint16_t remaining = angle_raw;

        for (glyph = GLYPH_COUNT; glyph != 0u; --glyph)
        {
            digits[glyph - 1u] = (uint8_t)(remaining % 10u);
            remaining = (uint16_t)(remaining / 10u);
        }
    }

    for (glyph = 0u; glyph < GLYPH_COUNT; ++glyph)
    {
        draw_glyph(pixels,
                   digits[glyph],
                   LABEL_X +
                       (glyph * ((GLYPH_WIDTH * GLYPH_SCALE) +
                                 GLYPH_SPACING)));
    }
    return true;
}
