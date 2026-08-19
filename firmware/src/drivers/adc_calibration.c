#include "mks57d/adc_calibration.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

static bool finite_in_range(float value, float minimum, float maximum)
{
    return isfinite(value) && (value >= minimum) && (value <= maximum);
}

bool adc_calibration_is_valid(const adc_calibration_t* calibration)
{
    return (calibration != NULL) &&
           isfinite(calibration->reference_voltage) &&
           (calibration->reference_voltage > 0.0f) &&
           finite_in_range(calibration->current_b_zero_raw,
                           0.0f,
                           (float)ADC_SAMPLE_RAW_MAX) &&
           finite_in_range(calibration->current_a_zero_raw,
                           0.0f,
                           (float)ADC_SAMPLE_RAW_MAX);
}

bool adc_calibration_build(adc_calibration_t* output,
                           float reference_voltage,
                           float current_b_zero_raw,
                           float current_a_zero_raw)
{
    const adc_calibration_t candidate = {
        .reference_voltage = reference_voltage,
        .current_b_zero_raw = current_b_zero_raw,
        .current_a_zero_raw = current_a_zero_raw,
    };

    if ((output == NULL) || !adc_calibration_is_valid(&candidate))
    {
        return false;
    }

    *output = candidate;
    return true;
}

bool adc_sample_convert(const adc_sample_t* raw,
                        const adc_calibration_t* calibration,
                        adc_engineering_sample_t* output)
{
    adc_engineering_sample_t candidate;
    float volts_per_count;
    float amperes_per_count;
    float vbus_ratio;

    if (!adc_sample_is_valid(raw) ||
        !adc_calibration_is_valid(calibration) ||
        (output == NULL))
    {
        return false;
    }

    volts_per_count =
        calibration->reference_voltage / (float)ADC_SAMPLE_RAW_MAX;
    amperes_per_count = volts_per_count /
                        (ADC_CURRENT_SHUNT_OHMS *
                         ADC_CURRENT_SENSE_GAIN);
    vbus_ratio =
        (ADC_VBUS_UPPER_RESISTANCE_OHMS +
         ADC_VBUS_LOWER_RESISTANCE_OHMS) /
        ADC_VBUS_LOWER_RESISTANCE_OHMS;

    candidate.current_b_amperes =
        ((float)raw->current_b_raw - calibration->current_b_zero_raw) *
        amperes_per_count;
    candidate.current_a_amperes =
        ((float)raw->current_a_raw - calibration->current_a_zero_raw) *
        amperes_per_count;
    candidate.vbus_volts =
        (float)raw->vbus_raw * volts_per_count * vbus_ratio;
    candidate.capture_index = raw->capture_index;

    *output = candidate;
    return true;
}

bool adc_zero_calibrator_init(adc_zero_calibrator_t* calibrator,
                              float reference_voltage)
{
    if ((calibrator == NULL) || !isfinite(reference_voltage) ||
        (reference_voltage <= 0.0f))
    {
        return false;
    }

    memset(calibrator, 0, sizeof(*calibrator));
    calibrator->reference_voltage = reference_voltage;
    return true;
}

bool adc_zero_calibrator_observe(adc_zero_calibrator_t* calibrator,
                                 uint16_t current_b_raw,
                                 uint16_t current_a_raw)
{
    if ((calibrator == NULL) ||
        (current_b_raw > ADC_SAMPLE_RAW_MAX) ||
        (current_a_raw > ADC_SAMPLE_RAW_MAX))
    {
        return false;
    }
    if (calibrator->complete)
    {
        return true;
    }
    if ((calibrator->sample_count >= ADC_ZERO_CALIBRATION_SAMPLE_COUNT) ||
        !isfinite(calibrator->reference_voltage) ||
        (calibrator->reference_voltage <= 0.0f))
    {
        return false;
    }

    calibrator->current_b_sum += current_b_raw;
    calibrator->current_a_sum += current_a_raw;
    ++calibrator->sample_count;

    if (calibrator->sample_count == ADC_ZERO_CALIBRATION_SAMPLE_COUNT)
    {
        const float divisor = (float)ADC_ZERO_CALIBRATION_SAMPLE_COUNT;

        calibrator->complete = adc_calibration_build(
            &calibrator->result,
            calibrator->reference_voltage,
            (float)calibrator->current_b_sum / divisor,
            (float)calibrator->current_a_sum / divisor);
        return calibrator->complete;
    }

    return true;
}

bool adc_zero_calibrator_get(const adc_zero_calibrator_t* calibrator,
                             adc_calibration_t* output)
{
    if ((calibrator == NULL) || (output == NULL) ||
        !calibrator->complete ||
        !adc_calibration_is_valid(&calibrator->result))
    {
        return false;
    }

    *output = calibrator->result;
    return true;
}

static int32_t round_milliamperes(float value)
{
    return value >= 0.0f ? (int32_t)(value + 0.5f) :
                           (int32_t)(value - 0.5f);
}

bool adc_current_pair_convert_milliamperes(
    uint16_t current_b_raw,
    uint16_t current_a_raw,
    const adc_calibration_t* calibration,
    int32_t* current_b_milliamperes,
    int32_t* current_a_milliamperes)
{
    float milliamperes_per_count;

    if ((current_b_raw > ADC_SAMPLE_RAW_MAX) ||
        (current_a_raw > ADC_SAMPLE_RAW_MAX) ||
        !adc_calibration_is_valid(calibration) ||
        (current_b_milliamperes == NULL) ||
        (current_a_milliamperes == NULL))
    {
        return false;
    }

    milliamperes_per_count =
        (calibration->reference_voltage * 1000.0f) /
        ((float)ADC_SAMPLE_RAW_MAX * ADC_CURRENT_SHUNT_OHMS *
         ADC_CURRENT_SENSE_GAIN);
    *current_b_milliamperes = round_milliamperes(
        ((float)current_b_raw - calibration->current_b_zero_raw) *
        milliamperes_per_count);
    *current_a_milliamperes = round_milliamperes(
        ((float)current_a_raw - calibration->current_a_zero_raw) *
        milliamperes_per_count);
    return true;
}
