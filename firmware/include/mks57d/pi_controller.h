#ifndef MKS57D_PI_CONTROLLER_H
#define MKS57D_PI_CONTROLLER_H

#include <stdbool.h>

typedef struct
{
    float proportional_gain;
    float integral_gain_per_second;
    float output_limit;
    float integrator_limit;
} pi_controller_config_t;

typedef struct
{
    float integrator;
    bool initialized;
} pi_controller_t;

bool pi_controller_config_is_valid(const pi_controller_config_t* config);
void pi_controller_reset(pi_controller_t* controller);
bool pi_controller_step(pi_controller_t* controller,
                        const pi_controller_config_t* config,
                        float error,
                        float elapsed_seconds,
                        float* output);

#endif
