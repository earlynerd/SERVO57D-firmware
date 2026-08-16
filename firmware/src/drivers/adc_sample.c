#include "mks57d/adc_sample.h"

#include <stddef.h>

bool adc_sample_build(adc_sample_t* output,
                      uint16_t current_b_raw,
                      uint16_t current_a_raw,
                      uint16_t vbus_raw,
                      uint32_t capture_index)
{
    if ((output == NULL) ||
        (current_b_raw > ADC_SAMPLE_RAW_MAX) ||
        (current_a_raw > ADC_SAMPLE_RAW_MAX) ||
        (vbus_raw > ADC_SAMPLE_RAW_MAX))
    {
        return false;
    }

    output->current_b_raw = current_b_raw;
    output->current_a_raw = current_a_raw;
    output->vbus_raw = vbus_raw;
    output->capture_index = capture_index;
    return true;
}

bool adc_sample_is_valid(const adc_sample_t* sample)
{
    return (sample != NULL) &&
           (sample->current_b_raw <= ADC_SAMPLE_RAW_MAX) &&
           (sample->current_a_raw <= ADC_SAMPLE_RAW_MAX) &&
           (sample->vbus_raw <= ADC_SAMPLE_RAW_MAX);
}
