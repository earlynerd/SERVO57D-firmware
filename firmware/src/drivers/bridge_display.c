#include "mks57d/bridge_display.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum
{
    GLYPH_WIDTH = 5u,
    GLYPH_HEIGHT = 7u,
    GLYPH_A = 0u,
    GLYPH_B,
    GLYPH_1,
    GLYPH_2,
    GLYPH_O,
    GLYPH_F,
    GLYPH_R,
    GLYPH_U,
    GLYPH_N,
    GLYPH_Z,
    GLYPH_E
};

static const uint8_t s_glyphs[][GLYPH_WIDTH] = {
    {0x7Eu, 0x11u, 0x11u, 0x11u, 0x7Eu}, /* A */
    {0x7Fu, 0x49u, 0x49u, 0x49u, 0x36u}, /* B */
    {0x00u, 0x42u, 0x7Fu, 0x40u, 0x00u}, /* 1 */
    {0x62u, 0x51u, 0x49u, 0x49u, 0x46u}, /* 2 */
    {0x3Eu, 0x41u, 0x41u, 0x41u, 0x3Eu}, /* O */
    {0x7Fu, 0x09u, 0x09u, 0x09u, 0x01u}, /* F */
    {0x7Fu, 0x09u, 0x19u, 0x29u, 0x46u}, /* R */
    {0x3Fu, 0x40u, 0x40u, 0x40u, 0x3Fu}, /* U */
    {0x7Fu, 0x02u, 0x0Cu, 0x10u, 0x7Fu}, /* N */
    {0x61u, 0x51u, 0x49u, 0x45u, 0x43u}, /* Z */
    {0x7Fu, 0x49u, 0x49u, 0x49u, 0x41u}, /* E */
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
                    ((pixel_y / 8u) * BRIDGE_DISPLAY_WIDTH) + x + column;

                pixels[pixel_index] |=
                    (uint8_t)(1u << (pixel_y % 8u));
            }
        }
    }
}

bool bridge_display_render(uint8_t* pixels,
                           size_t length,
                           bridge_characterizer_leg_t selected_leg,
                           bool active)
{
    const size_t phase_glyph =
        selected_leg < BRIDGE_CHARACTERIZER_LEG_B1 ? GLYPH_A : GLYPH_B;
    const size_t leg_glyph =
        ((selected_leg == BRIDGE_CHARACTERIZER_LEG_A1) ||
         (selected_leg == BRIDGE_CHARACTERIZER_LEG_B1)) ? GLYPH_1 : GLYPH_2;

    if ((pixels == NULL) || (length != BRIDGE_DISPLAY_FRAME_BYTES) ||
        (selected_leg >= BRIDGE_CHARACTERIZER_LEG_COUNT))
    {
        return false;
    }

    memset(pixels, 0, length);
    draw_glyph(pixels, phase_glyph, 29u, 0u);
    draw_glyph(pixels, leg_glyph, 37u, 0u);

    if (active)
    {
        draw_glyph(pixels, GLYPH_R, 25u, 8u);
        draw_glyph(pixels, GLYPH_U, 33u, 8u);
        draw_glyph(pixels, GLYPH_N, 41u, 8u);
    }
    else
    {
        draw_glyph(pixels, GLYPH_Z, 21u, 8u);
        draw_glyph(pixels, GLYPH_E, 29u, 8u);
        draw_glyph(pixels, GLYPH_R, 37u, 8u);
        draw_glyph(pixels, GLYPH_O, 45u, 8u);
    }

    return true;
}
