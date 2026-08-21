#include "mks57d/motor_alignment.h"

#include <limits.h>
#include <stddef.h>

static int32_t wrapped_delta(uint16_t from,
                             uint16_t to,
                             uint16_t counts_per_revolution)
{
    int32_t delta = (int32_t)to - (int32_t)from;
    const int32_t half_revolution =
        (int32_t)counts_per_revolution / 2;

    if (delta > half_revolution)
    {
        delta -= (int32_t)counts_per_revolution;
    }
    else if (delta < -half_revolution)
    {
        delta += (int32_t)counts_per_revolution;
    }
    return delta;
}

bool motor_alignment_config_is_valid(
    const motor_alignment_config_t* config)
{
    uint32_t quarter_step_denominator;

    if ((config == NULL) ||
        (config->encoder_counts_per_revolution < 8u) ||
        (config->electrical_cycles_per_revolution == 0u) ||
        (config->maximum_quarter_step_error_counts == 0u))
    {
        return false;
    }

    quarter_step_denominator =
        (uint32_t)config->electrical_cycles_per_revolution * 4u;
    return quarter_step_denominator <=
           ((uint32_t)config->encoder_counts_per_revolution / 2u);
}

bool motor_alignment_init(motor_alignment_t* alignment,
                          const motor_alignment_config_t* config)
{
    const motor_alignment_status_t empty_status = {0};

    if ((alignment == NULL) ||
        !motor_alignment_config_is_valid(config))
    {
        return false;
    }

    alignment->config = *config;
    alignment->status = empty_status;
    alignment->initialized = true;
    return true;
}

bool motor_alignment_calibrate(motor_alignment_t* alignment,
                               uint16_t phase_zero_raw,
                               uint16_t phase_quarter_raw)
{
    int32_t delta;
    uint32_t magnitude;
    uint32_t denominator;
    uint32_t expected_rounded;
    uint32_t scaled_error;
    uint32_t allowed_scaled_error;
    int32_t quarter_step_error;

    if ((alignment == NULL) || !alignment->initialized ||
        ((uint32_t)phase_zero_raw >=
         alignment->config.encoder_counts_per_revolution) ||
        ((uint32_t)phase_quarter_raw >=
         alignment->config.encoder_counts_per_revolution))
    {
        return false;
    }

    delta = wrapped_delta(
        phase_zero_raw,
        phase_quarter_raw,
        alignment->config.encoder_counts_per_revolution);
    if (delta == 0)
    {
        return false;
    }

    magnitude = (uint32_t)((delta < 0) ? -delta : delta);
    denominator =
        (uint32_t)alignment->config.electrical_cycles_per_revolution * 4u;
    expected_rounded =
        ((uint32_t)alignment->config.encoder_counts_per_revolution +
         (denominator / 2u)) /
        denominator;
    scaled_error =
        (magnitude * denominator >
         (uint32_t)alignment->config.encoder_counts_per_revolution) ?
            (magnitude * denominator -
             (uint32_t)alignment->config.encoder_counts_per_revolution) :
            ((uint32_t)alignment->config.encoder_counts_per_revolution -
             magnitude * denominator);
    allowed_scaled_error =
        (uint32_t)alignment->config.maximum_quarter_step_error_counts *
        denominator;
    if (scaled_error > allowed_scaled_error)
    {
        return false;
    }
    quarter_step_error =
        (int32_t)magnitude - (int32_t)expected_rounded;
    if ((quarter_step_error < INT16_MIN) ||
        (quarter_step_error > INT16_MAX))
    {
        return false;
    }

    alignment->status.electrical_zero_raw = phase_zero_raw;
    alignment->status.observed_quarter_step_counts = (uint16_t)magnitude;
    alignment->status.quarter_step_error_counts =
        (int16_t)quarter_step_error;
    alignment->status.encoder_direction = (delta > 0) ? 1 : -1;
    alignment->status.valid = true;
    return true;
}

bool motor_alignment_restore(motor_alignment_t* alignment,
                             const motor_alignment_status_t* status)
{
    motor_alignment_t candidate;
    uint32_t phase_quarter_raw;

    if ((alignment == NULL) || (status == NULL) ||
        !alignment->initialized || !status->valid ||
        ((status->encoder_direction != 1) &&
         (status->encoder_direction != -1)) ||
        ((uint32_t)status->electrical_zero_raw >=
         alignment->config.encoder_counts_per_revolution) ||
        (status->observed_quarter_step_counts == 0u) ||
        ((uint32_t)status->observed_quarter_step_counts >=
         ((uint32_t)alignment->config.encoder_counts_per_revolution / 2u)))
    {
        return false;
    }

    phase_quarter_raw =
        (uint32_t)status->electrical_zero_raw +
        ((status->encoder_direction > 0) ?
             (uint32_t)status->observed_quarter_step_counts :
             (uint32_t)alignment->config.encoder_counts_per_revolution -
                 (uint32_t)status->observed_quarter_step_counts);
    phase_quarter_raw %=
        (uint32_t)alignment->config.encoder_counts_per_revolution;

    candidate = *alignment;
    if (!motor_alignment_calibrate(
            &candidate,
            status->electrical_zero_raw,
            (uint16_t)phase_quarter_raw) ||
        (candidate.status.observed_quarter_step_counts !=
         status->observed_quarter_step_counts) ||
        (candidate.status.quarter_step_error_counts !=
         status->quarter_step_error_counts) ||
        (candidate.status.encoder_direction !=
         status->encoder_direction))
    {
        return false;
    }

    alignment->status = candidate.status;
    return true;
}

void motor_alignment_clear(motor_alignment_t* alignment)
{
    const motor_alignment_status_t empty_status = {0};

    if (alignment != NULL)
    {
        alignment->status = empty_status;
    }
}

bool motor_alignment_electrical_phase_q32(
    const motor_alignment_t* alignment,
    uint16_t encoder_raw,
    uint32_t* electrical_phase_q32)
{
    int64_t phase_numerator;
    int64_t phase_remainder;

    if ((alignment == NULL) || (electrical_phase_q32 == NULL) ||
        !alignment->initialized || !alignment->status.valid ||
        ((uint32_t)encoder_raw >=
         alignment->config.encoder_counts_per_revolution) ||
        ((alignment->status.encoder_direction != 1) &&
         (alignment->status.encoder_direction != -1)))
    {
        return false;
    }

    phase_numerator =
        ((int64_t)(int32_t)encoder_raw -
         (int64_t)(int32_t)alignment->status.electrical_zero_raw) *
        (int64_t)alignment->status.encoder_direction *
        (int64_t)alignment->config.electrical_cycles_per_revolution;
    phase_remainder =
        phase_numerator %
        (int64_t)alignment->config.encoder_counts_per_revolution;
    if (phase_remainder < 0)
    {
        phase_remainder +=
            (int64_t)alignment->config.encoder_counts_per_revolution;
    }

    *electrical_phase_q32 = (uint32_t)(
        ((uint64_t)phase_remainder << 32u) /
        (uint64_t)alignment->config.encoder_counts_per_revolution);
    return true;
}

void motor_alignment_get_status(const motor_alignment_t* alignment,
                                motor_alignment_status_t* status)
{
    const motor_alignment_status_t empty_status = {0};

    if (status == NULL)
    {
        return;
    }
    *status = ((alignment != NULL) && alignment->initialized) ?
        alignment->status : empty_status;
}
