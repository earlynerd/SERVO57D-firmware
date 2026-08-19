#ifndef MKS57D_ADC_CALIBRATION_H
#define MKS57D_ADC_CALIBRATION_H

#include <stdbool.h>
#include <stdint.h>

#include "mks57d/adc_sample.h"

/* Schematic-derived analog front-end constants. The current amplifiers obey
   Vout = Vref + 6.65 * (Vkelvin+ - Vkelvin-), so the mid-rail bias has unity
   gain while the 20 mOhm shunt voltage has a gain of 6.65. */
#define ADC_CURRENT_SHUNT_OHMS (0.020f)
#define ADC_CURRENT_SENSE_GAIN (6.65f)
#define ADC_VBUS_UPPER_RESISTANCE_OHMS (15400.0f)
#define ADC_VBUS_LOWER_RESISTANCE_OHMS (1000.0f)
#define ADC_NOMINAL_REFERENCE_VOLTS (3.3f)

enum
{
    ADC_ZERO_CALIBRATION_SAMPLE_COUNT = 32u
};

typedef struct
{
    float reference_voltage;
    float current_b_zero_raw;
    float current_a_zero_raw;
} adc_calibration_t;

typedef struct
{
    float current_b_amperes;
    float current_a_amperes;
    float vbus_volts;
    uint32_t capture_index;
} adc_engineering_sample_t;

typedef struct
{
    uint32_t current_b_sum;
    uint32_t current_a_sum;
    uint32_t sample_count;
    float reference_voltage;
    adc_calibration_t result;
    bool complete;
} adc_zero_calibrator_t;

bool adc_calibration_build(adc_calibration_t* output,
                           float reference_voltage,
                           float current_b_zero_raw,
                           float current_a_zero_raw);
bool adc_calibration_is_valid(const adc_calibration_t* calibration);
bool adc_sample_convert(const adc_sample_t* raw,
                        const adc_calibration_t* calibration,
                        adc_engineering_sample_t* output);
bool adc_zero_calibrator_init(adc_zero_calibrator_t* calibrator,
                              float reference_voltage);
bool adc_zero_calibrator_observe(adc_zero_calibrator_t* calibrator,
                                 uint16_t current_b_raw,
                                 uint16_t current_a_raw);
bool adc_zero_calibrator_get(const adc_zero_calibrator_t* calibrator,
                             adc_calibration_t* output);
bool adc_current_pair_convert_milliamperes(
    uint16_t current_b_raw,
    uint16_t current_a_raw,
    const adc_calibration_t* calibration,
    int32_t* current_b_milliamperes,
    int32_t* current_a_milliamperes);

#endif
