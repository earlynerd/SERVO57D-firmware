#ifndef MKS57D_ELECTRICAL_PHASE_PREDICTOR_H
#define MKS57D_ELECTRICAL_PHASE_PREDICTOR_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    uint16_t electrical_cycles_per_mechanical_revolution;
    uint16_t output_lead_us;
    uint32_t maximum_prediction_age_us;
    int32_t maximum_mechanical_velocity_q16_16;
} electrical_phase_predictor_config_t;

typedef struct
{
    electrical_phase_predictor_config_t config;
    uint32_t observed_electrical_phase_q32;
    uint32_t observation_timestamp_us;
    int32_t electrical_phase_rate_q32_per_us;
    bool observation_valid;
    bool initialized;
} electrical_phase_predictor_t;

bool electrical_phase_predictor_config_is_valid(
    const electrical_phase_predictor_config_t* config);
bool electrical_phase_predictor_init(
    electrical_phase_predictor_t* predictor,
    const electrical_phase_predictor_config_t* config);
bool electrical_phase_predictor_set_observation(
    electrical_phase_predictor_t* predictor,
    uint32_t electrical_phase_q32,
    int32_t mechanical_velocity_revolutions_per_second_q16_16,
    int8_t encoder_direction,
    uint32_t observation_timestamp_us);
bool electrical_phase_predictor_predict(
    const electrical_phase_predictor_t* predictor,
    uint32_t now_us,
    uint32_t* electrical_phase_q32,
    uint32_t* prediction_age_us);
void electrical_phase_predictor_reset(
    electrical_phase_predictor_t* predictor);

#endif
