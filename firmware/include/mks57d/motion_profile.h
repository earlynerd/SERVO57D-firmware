#ifndef MKS57D_MOTION_PROFILE_H
#define MKS57D_MOTION_PROFILE_H

#include <stdbool.h>

typedef struct
{
    float maximum_velocity_revolutions_per_second;
    float maximum_acceleration_revolutions_per_second_squared;
    float maximum_step_seconds;
    float position_tolerance_revolutions;
    float velocity_tolerance_revolutions_per_second;
} motion_profile_config_t;

typedef struct
{
    float position_revolutions;
    float velocity_revolutions_per_second;
    float target_position_revolutions;
    bool initialized;
} motion_profile_t;

bool motion_profile_config_is_valid(const motion_profile_config_t* config);
bool motion_profile_init(motion_profile_t* profile,
                         float initial_position_revolutions);
bool motion_profile_set_target(motion_profile_t* profile,
                               float target_position_revolutions);
bool motion_profile_step(motion_profile_t* profile,
                         const motion_profile_config_t* config,
                         float elapsed_seconds);
bool motion_profile_is_settled(const motion_profile_t* profile,
                               const motion_profile_config_t* config);

#endif
