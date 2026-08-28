#include "mks57d/rotor_display.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum
{
    GLYPH_WIDTH = 5u,
    GLYPH_HEIGHT = 7u,
    GLYPH_SPACING = 1u,
    CHARACTER_COUNT = 9u,
    ROW_WIDTH = (CHARACTER_COUNT * GLYPH_WIDTH) +
                ((CHARACTER_COUNT - 1u) * GLYPH_SPACING),
    ROW_X = (ROTOR_DISPLAY_WIDTH - ROW_WIDTH) / 2u,
    POSITION_ROW_Y = 0u,
    VELOCITY_ROW_Y = 8u,
    MINUS_GLYPH = 10u,
    PLUS_GLYPH = 11u,
    POSITION_GLYPH = 12u,
    VELOCITY_GLYPH = 13u,
    DECIMAL_GLYPH = 14u,
    MAXIMUM_MILLI_UNITS = 999999u
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
    {0x08u, 0x08u, 0x3Eu, 0x08u, 0x08u}, /* + */
    {0x7Fu, 0x09u, 0x09u, 0x09u, 0x06u}, /* P */
    {0x1Fu, 0x20u, 0x40u, 0x20u, 0x1Fu}, /* V */
    {0x00u, 0x60u, 0x60u, 0x00u, 0x00u}, /* . */
};

static void set_pixel(uint8_t* pixels, size_t x, size_t y)
{
    const size_t index = ((y / 8u) * ROTOR_DISPLAY_WIDTH) + x;

    pixels[index] |= (uint8_t)(1u << (y % 8u));
}

static void draw_glyph(uint8_t* pixels,
                       size_t glyph_index,
                       size_t x,
                       size_t y)
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
                set_pixel(pixels, x + column, y + row);
            }
        }
    }
}

static bool value_to_milli_units(float value, int32_t* milli_units)
{
    float scaled;
    int32_t rounded;

    if ((milli_units == NULL) || (value != value) ||
        (value <= -1000.0f) || (value >= 1000.0f))
    {
        return false;
    }

    scaled = value * 1000.0f;
    rounded = scaled < 0.0f ? (int32_t)(scaled - 0.5f) :
                              (int32_t)(scaled + 0.5f);
    if ((rounded < -(int32_t)MAXIMUM_MILLI_UNITS) ||
        (rounded > (int32_t)MAXIMUM_MILLI_UNITS))
    {
        return false;
    }
    *milli_units = rounded;
    return true;
}

static void draw_measurement(uint8_t* pixels,
                             size_t label_glyph,
                             size_t y,
                             float value,
                             bool valid)
{
    uint8_t glyphs[CHARACTER_COUNT];
    int32_t milli_units = 0;
    uint32_t magnitude;
    size_t index;

    glyphs[0] = (uint8_t)label_glyph;
    glyphs[5] = DECIMAL_GLYPH;
    if (!valid || !value_to_milli_units(value, &milli_units))
    {
        for (index = 1u; index < CHARACTER_COUNT; ++index)
        {
            glyphs[index] = index == 5u ? DECIMAL_GLYPH : MINUS_GLYPH;
        }
    }
    else
    {
        glyphs[1] = milli_units < 0 ? MINUS_GLYPH : PLUS_GLYPH;
        magnitude = milli_units < 0 ? (uint32_t)(-milli_units) :
                                      (uint32_t)milli_units;
        for (index = CHARACTER_COUNT; index != 6u; --index)
        {
            glyphs[index - 1u] = (uint8_t)(magnitude % 10u);
            magnitude /= 10u;
        }
        for (index = 5u; index != 2u; --index)
        {
            glyphs[index - 1u] = (uint8_t)(magnitude % 10u);
            magnitude /= 10u;
        }
    }

    for (index = 0u; index < CHARACTER_COUNT; ++index)
    {
        draw_glyph(pixels,
                   glyphs[index],
                   ROW_X + (index * (GLYPH_WIDTH + GLYPH_SPACING)),
                   y);
    }
}

bool rotor_display_render(uint8_t* pixels,
                          size_t length,
                          float position_revolutions,
                          float velocity_revolutions_per_second,
                          bool valid)
{
    if ((pixels == NULL) || (length != ROTOR_DISPLAY_FRAME_BYTES))
    {
        return false;
    }

    memset(pixels, 0, length);
    draw_measurement(pixels,
                     POSITION_GLYPH,
                     POSITION_ROW_Y,
                     position_revolutions,
                     valid);
    draw_measurement(pixels,
                     VELOCITY_GLYPH,
                     VELOCITY_ROW_Y,
                     velocity_revolutions_per_second,
                     valid);
    return true;
}
