#ifndef MKS57D_ADC_SAMPLE_H
#define MKS57D_ADC_SAMPLE_H

#include <stdbool.h>
#include <stdint.h>

enum
{
    ADC_SAMPLE_RAW_MAX = 4095u
};

/*
 * Raw, unscaled observations only. The channel order mirrors the schematic:
 * PA1 currentB, PA2 currentA, then PA3 vBus.
 */
typedef struct
{
    uint16_t current_b_raw;
    uint16_t current_a_raw;
    uint16_t vbus_raw;
    uint32_t capture_index;
} adc_sample_t;

bool adc_sample_build(adc_sample_t* output,
                      uint16_t current_b_raw,
                      uint16_t current_a_raw,
                      uint16_t vbus_raw,
                      uint32_t capture_index);
bool adc_sample_is_valid(const adc_sample_t* sample);

#endif
