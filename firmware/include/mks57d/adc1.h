#ifndef MKS57D_ADC1_H
#define MKS57D_ADC1_H

#include <stdint.h>

#include "mks57d/adc_sample.h"

enum
{
    ADC1_CURRENT_B_CHANNEL = 2u,
    ADC1_CURRENT_A_CHANNEL = 3u,
    ADC1_VBUS_CHANNEL = 4u,
    ADC1_PASSIVE_MAX_CLOCK_HZ = 2000000u
};

typedef enum
{
    ADC1_STATUS_OK = 0,
    ADC1_STATUS_INVALID_ARGUMENT,
    ADC1_STATUS_NOT_READY,
    ADC1_STATUS_UNSUPPORTED_CLOCK,
    ADC1_STATUS_CLOCK_TIMEOUT,
    ADC1_STATUS_POWER_TIMEOUT,
    ADC1_STATUS_CALIBRATION_TIMEOUT,
    ADC1_STATUS_BUSY,
    ADC1_STATUS_CONVERSION_TIMEOUT,
    ADC1_STATUS_DATA_OUT_OF_RANGE
} adc1_status_t;

/*
 * Provisional bridge-disabled acquisition for PA1/PA2/PA3. Merely linking
 * this module does not enable HSI, ADC, or GPIOA; init must be called.
 * The hclk_hz argument must describe the actual HCLK frequency.
 */
adc1_status_t adc1_init_passive(uint32_t hclk_hz);
adc1_status_t adc1_read_passive(adc_sample_t* output);

#endif
