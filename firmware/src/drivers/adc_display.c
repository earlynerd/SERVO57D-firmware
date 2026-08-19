#include "mks57d/adc_display.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "mks57d/adc_sample.h"

enum
{
    GLYPH_WIDTH = 5u,
    GLYPH_HEIGHT = 7u,
    GLYPH_SCALE = 2u,
    GLYPH_SPACING = 2u,
    DIGIT_COUNT = 4u,
    CHARACTER_COUNT = DIGIT_COUNT + 1u,
    LABEL_SPACING = 2u,
    DISPLAYED_WIDTH = (CHARACTER_COUNT * GLYPH_WIDTH * GLYPH_SCALE) +
                      ((DIGIT_COUNT - 1u) * GLYPH_SPACING) +
                      LABEL_SPACING,
    DISPLAY_X = (ADC_DISPLAY_WIDTH - DISPLAYED_WIDTH) / 2u,
    DISPLAY_Y = 1u,
    INVALID_GLYPH = 10u,
    LABEL_A_GLYPH = 11u,
    LABEL_B_GLYPH = 12u,
    LABEL_V_GLYPH = 13u,
    PLUS_GLYPH = 14u,
    LOWER_M_GLYPH = 15u,
    LABEL_F_GLYPH = 16u,
    COMPACT_DIGIT_COUNT = 5u,
    COMPACT_CHARACTER_COUNT = 9u,
    COMPACT_SPACING = 1u,
    COMPACT_WIDTH =
        (COMPACT_CHARACTER_COUNT * GLYPH_WIDTH) +
        ((COMPACT_CHARACTER_COUNT - 1u) * COMPACT_SPACING),
    COMPACT_X = (ADC_DISPLAY_WIDTH - COMPACT_WIDTH) / 2u,
    COMPACT_A_Y = 0u,
    COMPACT_B_Y = 8u,
    COMPACT_MAX_MILLIAMPERES = 99999u
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
    {0x7Eu, 0x11u, 0x11u, 0x11u, 0x7Eu}, /* A */
    {0x7Fu, 0x49u, 0x49u, 0x49u, 0x36u}, /* B */
    {0x1Fu, 0x20u, 0x40u, 0x20u, 0x1Fu}, /* V */
    {0x08u, 0x08u, 0x3Eu, 0x08u, 0x08u}, /* + */
    {0x7Cu, 0x04u, 0x18u, 0x04u, 0x78u}, /* m */
    {0x7Fu, 0x09u, 0x09u, 0x09u, 0x01u}, /* F */
};

static void set_pixel(uint8_t* pixels, size_t x, size_t y)
{
    const size_t index = ((y / 8u) * ADC_DISPLAY_WIDTH) + x;

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
                                  DISPLAY_Y + (row * GLYPH_SCALE) + scale_y);
                    }
                }
            }
        }
    }
}

static void draw_compact_glyph(uint8_t* pixels,
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

static void draw_compact_current(uint8_t* pixels,
                                 size_t label_glyph_index,
                                 size_t y,
                                 int32_t milliamperes,
                                 bool valid)
{
    uint8_t glyphs[COMPACT_CHARACTER_COUNT];
    size_t index;

    glyphs[0] = (uint8_t)label_glyph_index;
    glyphs[7] = LOWER_M_GLYPH;
    glyphs[8] = LABEL_A_GLYPH;
    if (!valid || (milliamperes > (int32_t)COMPACT_MAX_MILLIAMPERES) ||
        (milliamperes < -(int32_t)COMPACT_MAX_MILLIAMPERES))
    {
        for (index = 1u; index <= 6u; ++index)
        {
            glyphs[index] = INVALID_GLYPH;
        }
    }
    else
    {
        uint32_t magnitude;

        glyphs[1] = milliamperes < 0 ? INVALID_GLYPH : PLUS_GLYPH;
        magnitude = milliamperes < 0 ? (uint32_t)(-milliamperes) :
                                       (uint32_t)milliamperes;
        for (index = 7u; index != 2u; --index)
        {
            glyphs[index - 1u] = (uint8_t)(magnitude % 10u);
            magnitude /= 10u;
        }
    }

    for (index = 0u; index < COMPACT_CHARACTER_COUNT; ++index)
    {
        draw_compact_glyph(
            pixels,
            glyphs[index],
            COMPACT_X + (index * (GLYPH_WIDTH + COMPACT_SPACING)),
            y);
    }
}

static size_t label_glyph(adc_display_channel_t channel)
{
    switch (channel)
    {
        case ADC_DISPLAY_CURRENT_A:
            return LABEL_A_GLYPH;
        case ADC_DISPLAY_CURRENT_B:
            return LABEL_B_GLYPH;
        case ADC_DISPLAY_VBUS:
            return LABEL_V_GLYPH;
        case ADC_DISPLAY_FAULT:
            return LABEL_F_GLYPH;
        default:
            return INVALID_GLYPH;
    }
}

bool adc_display_render(uint8_t* pixels,
                        size_t length,
                        adc_display_channel_t channel,
                        uint16_t raw_value,
                        bool valid)
{
    uint8_t digits[DIGIT_COUNT];
    size_t digit;

    if ((pixels == NULL) ||
        (length != ADC_DISPLAY_FRAME_BYTES) ||
        (channel >= ADC_DISPLAY_CHANNEL_COUNT))
    {
        return false;
    }

    memset(pixels, 0, length);
    if (!valid || (raw_value > ADC_SAMPLE_RAW_MAX))
    {
        for (digit = 0u; digit < DIGIT_COUNT; ++digit)
        {
            digits[digit] = INVALID_GLYPH;
        }
    }
    else
    {
        uint16_t remaining = raw_value;

        for (digit = DIGIT_COUNT; digit != 0u; --digit)
        {
            digits[digit - 1u] = (uint8_t)(remaining % 10u);
            remaining = (uint16_t)(remaining / 10u);
        }
    }

    draw_glyph(pixels, label_glyph(channel), DISPLAY_X);
    for (digit = 0u; digit < DIGIT_COUNT; ++digit)
    {
        draw_glyph(pixels,
                   digits[digit],
                   DISPLAY_X + (GLYPH_WIDTH * GLYPH_SCALE) +
                       LABEL_SPACING +
                       (digit * ((GLYPH_WIDTH * GLYPH_SCALE) +
                                 GLYPH_SPACING)));
    }
    return true;
}

bool adc_display_render_currents_milliamperes(
    uint8_t* pixels,
    size_t length,
    int32_t current_a_milliamperes,
    int32_t current_b_milliamperes,
    bool valid)
{
    if ((pixels == NULL) || (length != ADC_DISPLAY_FRAME_BYTES))
    {
        return false;
    }

    memset(pixels, 0, length);
    draw_compact_current(pixels,
                         LABEL_A_GLYPH,
                         COMPACT_A_Y,
                         current_a_milliamperes,
                         valid);
    draw_compact_current(pixels,
                         LABEL_B_GLYPH,
                         COMPACT_B_Y,
                         current_b_milliamperes,
                         valid);
    return true;
}
