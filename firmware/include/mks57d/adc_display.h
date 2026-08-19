#ifndef MKS57D_ADC_DISPLAY_H
#define MKS57D_ADC_DISPLAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum
{
    ADC_DISPLAY_WIDTH = 72u,
    ADC_DISPLAY_START_PAGE = 1u,
    ADC_DISPLAY_PAGE_COUNT = 2u,
    ADC_DISPLAY_FRAME_BYTES =
        ADC_DISPLAY_WIDTH * ADC_DISPLAY_PAGE_COUNT
};

typedef enum
{
    ADC_DISPLAY_CURRENT_A = 0,
    ADC_DISPLAY_CURRENT_B,
    ADC_DISPLAY_VBUS,
    ADC_DISPLAY_CHANNEL_COUNT
} adc_display_channel_t;

/* Render one labeled, zero-padded 12-bit raw ADC reading. Invalid samples
   retain the channel label and replace the four digits with dashes. */
bool adc_display_render(uint8_t* pixels,
                        size_t length,
                        adc_display_channel_t channel,
                        uint16_t raw_value,
                        bool valid);

/* Render simultaneous signed A/B currents in milliamperes using two compact
   5-by-7 rows: A+#####mA and B+#####mA. */
bool adc_display_render_currents_milliamperes(
    uint8_t* pixels,
    size_t length,
    int32_t current_a_milliamperes,
    int32_t current_b_milliamperes,
    bool valid);

#endif
