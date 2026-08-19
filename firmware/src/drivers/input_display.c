#include "mks57d/input_display.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "mks57d/user_inputs.h"

enum
{
    GLYPH_WIDTH = 5u,
    GLYPH_HEIGHT = 7u,
    CELL_START_X = 2u,
    CELL_PITCH = 14u,
    LABEL_Y = 0u,
    LEVEL_Y = 8u,
    GLYPH_ZERO = 0u,
    GLYPH_ONE = 1u,
    GLYPH_TWO = 2u,
    GLYPH_DASH = 3u,
    GLYPH_E = 4u,
    GLYPH_M = 5u,
    GLYPH_N = 6u
};

static const uint8_t s_glyphs[][GLYPH_WIDTH] = {
    {0x3Eu, 0x51u, 0x49u, 0x45u, 0x3Eu}, /* 0 */
    {0x00u, 0x42u, 0x7Fu, 0x40u, 0x00u}, /* 1 */
    {0x42u, 0x61u, 0x51u, 0x49u, 0x46u}, /* 2 */
    {0x08u, 0x08u, 0x08u, 0x08u, 0x08u}, /* - */
    {0x7Fu, 0x49u, 0x49u, 0x49u, 0x41u}, /* E */
    {0x7Fu, 0x02u, 0x0Cu, 0x02u, 0x7Fu}, /* M */
    {0x7Fu, 0x02u, 0x04u, 0x08u, 0x7Fu}, /* N */
};

static const uint8_t s_label_glyphs[USER_INPUT_LOCAL_COUNT] = {
    GLYPH_E,
    GLYPH_M,
    GLYPH_N,
    GLYPH_ONE,
    GLYPH_TWO,
};

static const uint32_t s_input_bits[USER_INPUT_LOCAL_COUNT] = {
    USER_INPUT_KEY_ENTER,
    USER_INPUT_KEY_MENU,
    USER_INPUT_KEY_NEXT,
    USER_INPUT_M_IN1,
    USER_INPUT_M_IN2,
};

static void draw_glyph(uint8_t* pixels,
                       size_t glyph_index,
                       size_t x,
                       size_t y)
{
    size_t column;

    for (column = 0u; column < GLYPH_WIDTH; ++column)
    {
        const uint8_t column_bits = s_glyphs[glyph_index][column];
        size_t row;

        for (row = 0u; row < GLYPH_HEIGHT; ++row)
        {
            if ((column_bits & (uint8_t)(1u << row)) != 0u)
            {
                const size_t pixel_y = y + row;
                const size_t pixel_index =
                    ((pixel_y / 8u) * INPUT_DISPLAY_WIDTH) + x + column;

                pixels[pixel_index] |=
                    (uint8_t)(1u << (pixel_y % 8u));
            }
        }
    }
}

bool input_display_render(uint8_t* pixels,
                          size_t length,
                          uint32_t raw_levels,
                          bool valid)
{
    size_t index;

    if ((pixels == NULL) || (length != INPUT_DISPLAY_FRAME_BYTES) ||
        ((raw_levels & ~USER_INPUT_MASK) != 0u))
    {
        return false;
    }

    memset(pixels, 0, length);
    for (index = 0u; index < USER_INPUT_LOCAL_COUNT; ++index)
    {
        const size_t x = CELL_START_X + (index * CELL_PITCH);
        size_t level_glyph = GLYPH_DASH;

        draw_glyph(pixels, s_label_glyphs[index], x, LABEL_Y);
        if (valid)
        {
            level_glyph = (raw_levels & s_input_bits[index]) != 0u
                              ? GLYPH_ONE
                              : GLYPH_ZERO;
        }
        draw_glyph(pixels, level_glyph, x, LEVEL_Y);
    }

    return true;
}
