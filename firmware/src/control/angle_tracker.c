#include "mks57d/angle_tracker.h"

#include <math.h>
#include <stddef.h>

static bool finite_positive(float value)
{
    return isfinite(value) && (value > 0.0f);
}

bool angle_tracker_config_is_valid(const angle_tracker_config_t* config)
{
    return (config != NULL) &&
           (config->counts_per_revolution >= 2u) &&
           (config->counts_per_revolution <= UINT16_MAX) &&
           (config->maximum_sample_interval_us != 0u) &&
           finite_positive(
               config->maximum_velocity_revolutions_per_second) &&
           isfinite(config->velocity_filter_alpha) &&
           (config->velocity_filter_alpha > 0.0f) &&
           (config->velocity_filter_alpha <= 1.0f);
}

bool angle_tracker_init(angle_tracker_t* tracker,
                        const angle_tracker_config_t* config)
{
    if ((tracker == NULL) || !angle_tracker_config_is_valid(config))
    {
        return false;
    }

    tracker->config = *config;
    tracker->last_timestamp_us = 0u;
    tracker->last_raw_angle = 0u;
    tracker->position_revolutions = 0.0f;
    tracker->velocity_revolutions_per_second = 0.0f;
    tracker->initialized = false;
    return true;
}

bool angle_tracker_push(angle_tracker_t* tracker,
                        uint16_t raw_angle,
                        uint32_t timestamp_us)
{
    uint32_t elapsed_us;
    int32_t delta_counts;
    int32_t half_revolution_counts;
    float elapsed_seconds;
    float delta_revolutions;
    float allowed_delta_revolutions;
    float instantaneous_velocity;

    if ((tracker == NULL) ||
        !angle_tracker_config_is_valid(&tracker->config) ||
        ((uint32_t)raw_angle >= tracker->config.counts_per_revolution))
    {
        return false;
    }

    if (!tracker->initialized)
    {
        tracker->last_raw_angle = raw_angle;
        tracker->last_timestamp_us = timestamp_us;
        tracker->position_revolutions =
            (float)raw_angle / (float)tracker->config.counts_per_revolution;
        tracker->velocity_revolutions_per_second = 0.0f;
        tracker->initialized = true;
        return true;
    }

    elapsed_us = timestamp_us - tracker->last_timestamp_us;
    if ((elapsed_us == 0u) ||
        (elapsed_us > tracker->config.maximum_sample_interval_us))
    {
        return false;
    }

    delta_counts = (int32_t)raw_angle - (int32_t)tracker->last_raw_angle;
    half_revolution_counts =
        (int32_t)(tracker->config.counts_per_revolution / 2u);
    if (delta_counts > half_revolution_counts)
    {
        delta_counts -= (int32_t)tracker->config.counts_per_revolution;
    }
    else if (delta_counts < -half_revolution_counts)
    {
        delta_counts += (int32_t)tracker->config.counts_per_revolution;
    }

    elapsed_seconds = (float)elapsed_us * 1.0e-6f;
    delta_revolutions =
        (float)delta_counts / (float)tracker->config.counts_per_revolution;
    allowed_delta_revolutions =
        (tracker->config.maximum_velocity_revolutions_per_second *
         elapsed_seconds) +
        (1.5f / (float)tracker->config.counts_per_revolution);
    if (fabsf(delta_revolutions) > allowed_delta_revolutions)
    {
        return false;
    }

    instantaneous_velocity = delta_revolutions / elapsed_seconds;
    tracker->velocity_revolutions_per_second +=
        tracker->config.velocity_filter_alpha *
        (instantaneous_velocity -
         tracker->velocity_revolutions_per_second);
    tracker->position_revolutions += delta_revolutions;
    tracker->last_raw_angle = raw_angle;
    tracker->last_timestamp_us = timestamp_us;
    return true;
}
