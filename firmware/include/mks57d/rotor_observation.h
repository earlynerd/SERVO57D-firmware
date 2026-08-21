#ifndef MKS57D_ROTOR_OBSERVATION_H
#define MKS57D_ROTOR_OBSERVATION_H

#include <stdbool.h>
#include <stdint.h>

/* Immutable output of the product-owned rotor estimator. Consumers may
 * validate and cache this observation, but must not reinterpret raw encoder
 * samples or maintain a second angle-unwrapping state. */
typedef struct
{
    float position_revolutions;
    float velocity_revolutions_per_second;
    uint32_t timestamp_us;
    bool valid;
} rotor_observation_t;

#endif
