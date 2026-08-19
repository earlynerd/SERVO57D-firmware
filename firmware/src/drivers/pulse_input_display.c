#include "mks57d/pulse_input_display.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "mks57d/user_inputs.h"

enum
{
    GLYPH_WIDTH = 5u,
    GLYPH_HEIGHT = 7u,
    INPUT_COUNT = 3u,
    CELL_START_X = 9u,
    CELL_PITCH = 24u,
    LABEL_Y = 0u,
    LEVEL_Y = 8u,
    GLYPH_ZERO = 0u,
    GLYPH_ONE = 1u,
    GLYPH_DASH = 2u,
    GLYPH_S = 3u,
    GLYPH_D = 4u,
    GLYPH_E = 5u
};

static const uint8_t s_glyphs[][GLYPH_WIDTH] = {
    {0x3Eu, 0x51u, 0x49u, 0x45u, 0x3Eu}, /* 0 */
    {0x00u, 0x42u, 0x7Fu, 0x40u, 0x00u}, /* 1 */
    {0x08u, 0x08u, 0x08u, 0x08u, 0x08u}, /* - */
    {0x46u, 0x49u, 0x49u, 0x49u, 0x31u}, /* S */
    {0x7Fu, 0x41u, 0x41u, 0x22u, 0x1Cu}, /* D */
    {0x7Fu, 0x49u, 0x49u, 0x49u, 0x41u}, /* E */
};

static const uint8_t s_label_glyphs[INPUT_COUNT] = {
    GLYPH_S,
    GLYPH_D,
    GLYPH_E,
};

static const uint32_t s_input_bits[INPUT_COUNT] = {
    USER_INPUT_STEP,
    USER_INPUT_DIRECTION,
    USER_INPUT_ENABLE,
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
                    ((pixel_y / 8u) * PULSE_INPUT_DISPLAY_WIDTH) + x + column;

                pixels[pixel_index] |=
                    (uint8_t)(1u << (pixel_y % 8u));
            }
        }
    }
}

bool pulse_input_display_render(uint8_t* pixels,
                                size_t length,
                                uint32_t raw_levels,
                                bool valid)
{
    size_t index;

    if ((pixels == NULL) ||
        (length != PULSE_INPUT_DISPLAY_FRAME_BYTES) ||
        ((raw_levels & ~USER_INPUT_MASK) != 0u))
    {
        return false;
    }

    memset(pixels, 0, length);
    for (index = 0u; index < INPUT_COUNT; ++index)
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
