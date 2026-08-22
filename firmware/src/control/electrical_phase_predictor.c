#include "mks57d/electrical_phase_predictor.h"

#include <limits.h>
#include <stddef.h>

enum
{
    Q16_SCALE = 1u << 16,
    MICROSECONDS_PER_SECOND = 1000000u
};

static bool phase_rate_from_velocity(
    const electrical_phase_predictor_config_t* config,
    int32_t mechanical_velocity_revolutions_per_second_q16_16,
    int8_t encoder_direction,
    int32_t* electrical_phase_rate_q32_per_us)
{
    int64_t numerator;
    int64_t rate;

    if ((config == NULL) ||
        (electrical_phase_rate_q32_per_us == NULL) ||
        ((encoder_direction != 1) && (encoder_direction != -1)) ||
        (mechanical_velocity_revolutions_per_second_q16_16 >
         config->maximum_mechanical_velocity_q16_16) ||
        (mechanical_velocity_revolutions_per_second_q16_16 <
         -config->maximum_mechanical_velocity_q16_16))
    {
        return false;
    }

    numerator =
        (int64_t)mechanical_velocity_revolutions_per_second_q16_16 *
        (int64_t)config->electrical_cycles_per_mechanical_revolution *
        (int64_t)Q16_SCALE * (int64_t)encoder_direction;
    numerator += numerator >= 0 ?
        (int64_t)MICROSECONDS_PER_SECOND / 2 :
        -((int64_t)MICROSECONDS_PER_SECOND / 2);
    rate = numerator / (int64_t)MICROSECONDS_PER_SECOND;
    if ((rate > INT32_MAX) || (rate < INT32_MIN))
    {
        return false;
    }

    *electrical_phase_rate_q32_per_us = (int32_t)rate;
    return true;
}

bool electrical_phase_predictor_config_is_valid(
    const electrical_phase_predictor_config_t* config)
{
    int32_t maximum_rate;

    return (config != NULL) &&
           (config->electrical_cycles_per_mechanical_revolution != 0u) &&
           (config->maximum_prediction_age_us != 0u) &&
           (config->maximum_prediction_age_us <= (uint32_t)INT32_MAX) &&
           ((uint32_t)config->output_lead_us <=
            config->maximum_prediction_age_us) &&
           (config->maximum_mechanical_velocity_q16_16 > 0) &&
           phase_rate_from_velocity(
               config,
               config->maximum_mechanical_velocity_q16_16,
               1,
               &maximum_rate);
}

bool electrical_phase_predictor_init(
    electrical_phase_predictor_t* predictor,
    const electrical_phase_predictor_config_t* config)
{
    if ((predictor == NULL) ||
        !electrical_phase_predictor_config_is_valid(config))
    {
        return false;
    }

    predictor->config = *config;
    predictor->observed_electrical_phase_q32 = 0u;
    predictor->observation_timestamp_us = 0u;
    predictor->electrical_phase_rate_q32_per_us = 0;
    predictor->observation_valid = false;
    predictor->initialized = true;
    return true;
}

bool electrical_phase_predictor_set_observation(
    electrical_phase_predictor_t* predictor,
    uint32_t electrical_phase_q32,
    int32_t mechanical_velocity_revolutions_per_second_q16_16,
    int8_t encoder_direction,
    uint32_t observation_timestamp_us)
{
    int32_t phase_rate;

    if ((predictor == NULL) || !predictor->initialized ||
        !phase_rate_from_velocity(
            &predictor->config,
            mechanical_velocity_revolutions_per_second_q16_16,
            encoder_direction,
            &phase_rate))
    {
        return false;
    }

    predictor->observed_electrical_phase_q32 = electrical_phase_q32;
    predictor->observation_timestamp_us = observation_timestamp_us;
    predictor->electrical_phase_rate_q32_per_us = phase_rate;
    predictor->observation_valid = true;
    return true;
}

bool electrical_phase_predictor_predict(
    const electrical_phase_predictor_t* predictor,
    uint32_t now_us,
    uint32_t* electrical_phase_q32,
    uint32_t* prediction_age_us)
{
    uint32_t age_us;
    uint32_t prediction_interval_us;
    int64_t phase_delta;

    if ((predictor == NULL) || (electrical_phase_q32 == NULL) ||
        !predictor->initialized || !predictor->observation_valid)
    {
        return false;
    }

    age_us = now_us - predictor->observation_timestamp_us;
    if (age_us > predictor->config.maximum_prediction_age_us)
    {
        return false;
    }
    prediction_interval_us =
        age_us + (uint32_t)predictor->config.output_lead_us;
    phase_delta =
        (int64_t)predictor->electrical_phase_rate_q32_per_us *
        (int64_t)prediction_interval_us;
    *electrical_phase_q32 =
        predictor->observed_electrical_phase_q32 + (uint32_t)phase_delta;
    if (prediction_age_us != NULL)
    {
        *prediction_age_us = age_us;
    }
    return true;
}

void electrical_phase_predictor_reset(
    electrical_phase_predictor_t* predictor)
{
    if ((predictor == NULL) || !predictor->initialized)
    {
        return;
    }

    predictor->observed_electrical_phase_q32 = 0u;
    predictor->observation_timestamp_us = 0u;
    predictor->electrical_phase_rate_q32_per_us = 0;
    predictor->observation_valid = false;
}
