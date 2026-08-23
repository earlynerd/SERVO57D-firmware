#include "mks57d/motion_profile.h"

#include <math.h>
#include <stddef.h>

#include "mks57d/control_math.h"

static float clampf(float value, float lower, float upper)
{
    if (value < lower)
    {
        return lower;
    }
    if (value > upper)
    {
        return upper;
    }
    return value;
}

bool motion_profile_config_is_valid(const motion_profile_config_t* config)
{
    return (config != NULL) &&
           finite_positive(
               config->maximum_velocity_revolutions_per_second) &&
           finite_positive(
               config->maximum_acceleration_revolutions_per_second_squared) &&
           finite_positive(config->maximum_step_seconds) &&
           finite_positive(config->position_tolerance_revolutions) &&
           finite_positive(
               config->velocity_tolerance_revolutions_per_second);
}

bool motion_profile_init(motion_profile_t* profile,
                         float initial_position_revolutions)
{
    if ((profile == NULL) || !isfinite(initial_position_revolutions))
    {
        return false;
    }

    profile->position_revolutions = initial_position_revolutions;
    profile->velocity_revolutions_per_second = 0.0f;
    profile->target_position_revolutions = initial_position_revolutions;
    profile->initialized = true;
    return true;
}

bool motion_profile_set_target(motion_profile_t* profile,
                               float target_position_revolutions)
{
    if ((profile == NULL) || !profile->initialized ||
        !isfinite(target_position_revolutions))
    {
        return false;
    }

    profile->target_position_revolutions = target_position_revolutions;
    return true;
}

bool motion_profile_request_stop(motion_profile_t* profile,
                                 const motion_profile_config_t* config)
{
    float stopping_distance;

    if ((profile == NULL) || !profile->initialized ||
        !motion_profile_config_is_valid(config) ||
        !isfinite(profile->position_revolutions) ||
        !isfinite(profile->velocity_revolutions_per_second))
    {
        return false;
    }

    stopping_distance =
        (profile->velocity_revolutions_per_second *
         fabsf(profile->velocity_revolutions_per_second)) /
        (2.0f *
         config->maximum_acceleration_revolutions_per_second_squared);
    profile->target_position_revolutions =
        profile->position_revolutions + stopping_distance;
    return isfinite(profile->target_position_revolutions);
}

bool motion_profile_step(motion_profile_t* profile,
                         const motion_profile_config_t* config,
                         float elapsed_seconds)
{
    float error;
    float stopping_speed;
    float desired_velocity;
    float maximum_velocity_change;
    float velocity_change;
    float previous_velocity;

    if ((profile == NULL) || !profile->initialized ||
        !motion_profile_config_is_valid(config) ||
        !finite_positive(elapsed_seconds) ||
        (elapsed_seconds > config->maximum_step_seconds))
    {
        return false;
    }

    error = profile->target_position_revolutions -
            profile->position_revolutions;
    if ((fabsf(error) <= config->position_tolerance_revolutions) &&
        (fabsf(profile->velocity_revolutions_per_second) <=
         config->velocity_tolerance_revolutions_per_second))
    {
        profile->position_revolutions =
            profile->target_position_revolutions;
        profile->velocity_revolutions_per_second = 0.0f;
        return true;
    }

    stopping_speed = sqrtf(
        2.0f *
        config->maximum_acceleration_revolutions_per_second_squared *
        fabsf(error));
    desired_velocity = fminf(
        config->maximum_velocity_revolutions_per_second,
        stopping_speed);
    if (error < 0.0f)
    {
        desired_velocity = -desired_velocity;
    }

    maximum_velocity_change =
        config->maximum_acceleration_revolutions_per_second_squared *
        elapsed_seconds;
    velocity_change = clampf(
        desired_velocity - profile->velocity_revolutions_per_second,
        -maximum_velocity_change,
        maximum_velocity_change);
    previous_velocity = profile->velocity_revolutions_per_second;
    profile->velocity_revolutions_per_second += velocity_change;
    profile->position_revolutions +=
        0.5f * (previous_velocity +
                profile->velocity_revolutions_per_second) *
        elapsed_seconds;

    if (!isfinite(profile->position_revolutions) ||
        !isfinite(profile->velocity_revolutions_per_second))
    {
        return false;
    }
    return true;
}

bool motion_profile_is_settled(const motion_profile_t* profile,
                               const motion_profile_config_t* config)
{
    return (profile != NULL) && profile->initialized &&
           motion_profile_config_is_valid(config) &&
           (fabsf(profile->target_position_revolutions -
                  profile->position_revolutions) <=
            config->position_tolerance_revolutions) &&
           (fabsf(profile->velocity_revolutions_per_second) <=
            config->velocity_tolerance_revolutions_per_second);
}
