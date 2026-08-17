#ifndef MKS57D_CURRENT_CONTROLLER_H
#define MKS57D_CURRENT_CONTROLLER_H

#include <stdbool.h>

#include "mks57d/pi_controller.h"

typedef struct
{
    float alpha;
    float beta;
} stationary_vector_t;

typedef struct
{
    float d;
    float q;
} rotating_vector_t;

typedef struct
{
    pi_controller_config_t d_axis;
    pi_controller_config_t q_axis;
    float maximum_voltage_magnitude;
    float vector_anti_windup_gain_per_second;
} current_controller_config_t;

typedef struct
{
    pi_controller_t d_axis;
    pi_controller_t q_axis;
    bool initialized;
} current_controller_t;

typedef struct
{
    rotating_vector_t measured_current;
    rotating_vector_t voltage_dq;
    stationary_vector_t voltage_alpha_beta;
    bool voltage_saturated;
} current_controller_output_t;

bool park_transform(stationary_vector_t stationary,
                    float electrical_angle_radians,
                    rotating_vector_t* rotating);
bool inverse_park_transform(rotating_vector_t rotating,
                            float electrical_angle_radians,
                            stationary_vector_t* stationary);
bool current_controller_config_is_valid(
    const current_controller_config_t* config);
bool current_controller_init(current_controller_t* controller,
                             const current_controller_config_t* config);
bool current_controller_step(current_controller_t* controller,
                             const current_controller_config_t* config,
                             stationary_vector_t measured_current,
                             rotating_vector_t requested_current,
                             float electrical_angle_radians,
                             float elapsed_seconds,
                             current_controller_output_t* output);

#endif
