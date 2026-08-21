#include "mks57d/alignment_controller.h"

#include <limits.h>
#include <stddef.h>

static bool state_is_active(alignment_controller_state_t state)
{
    return (state >= ALIGNMENT_CONTROLLER_STATE_PHASE_ZERO_SETTLE) &&
           (state <= ALIGNMENT_CONTROLLER_STATE_RETURN_ZERO_SAMPLE);
}

static int32_t absolute_i32(int32_t value)
{
    return (value < 0) ? -value : value;
}

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

static void sampler_reset(alignment_controller_sampler_t* sampler)
{
    const alignment_controller_sampler_t empty = {0};

    *sampler = empty;
}

static bool sampler_observe(alignment_controller_sampler_t* sampler,
                            uint16_t encoder_raw,
                            uint16_t counts_per_revolution,
                            uint16_t maximum_span_counts)
{
    int32_t delta;

    if (!sampler->initialized)
    {
        sampler->origin_raw = encoder_raw;
        sampler->initialized = true;
    }
    delta = wrapped_delta(
        sampler->origin_raw, encoder_raw, counts_per_revolution);
    if (sampler->sample_count == 0u)
    {
        sampler->minimum_delta_counts = delta;
        sampler->maximum_delta_counts = delta;
    }
    else
    {
        if (delta < sampler->minimum_delta_counts)
        {
            sampler->minimum_delta_counts = delta;
        }
        if (delta > sampler->maximum_delta_counts)
        {
            sampler->maximum_delta_counts = delta;
        }
    }
    if ((sampler->maximum_delta_counts -
         sampler->minimum_delta_counts) >
        (int32_t)maximum_span_counts)
    {
        return false;
    }
    if (sampler->sample_count == UINT16_MAX)
    {
        return false;
    }
    sampler->delta_sum_counts += delta;
    ++sampler->sample_count;
    return true;
}

static bool sampler_mean(const alignment_controller_sampler_t* sampler,
                         uint16_t counts_per_revolution,
                         uint16_t* mean_raw)
{
    int64_t rounded_sum;
    int32_t mean_delta;
    int32_t mean;

    if ((sampler == NULL) || (mean_raw == NULL) ||
        !sampler->initialized || (sampler->sample_count == 0u))
    {
        return false;
    }
    rounded_sum = sampler->delta_sum_counts;
    if (rounded_sum >= 0)
    {
        rounded_sum += sampler->sample_count / 2u;
    }
    else
    {
        rounded_sum -= sampler->sample_count / 2u;
    }
    mean_delta = (int32_t)(rounded_sum / sampler->sample_count);
    mean = (int32_t)sampler->origin_raw + mean_delta;
    while (mean < 0)
    {
        mean += counts_per_revolution;
    }
    while (mean >= (int32_t)counts_per_revolution)
    {
        mean -= counts_per_revolution;
    }
    *mean_raw = (uint16_t)mean;
    return true;
}

static void set_reference(alignment_controller_t* controller,
                          int16_t current_a_reference_counts,
                          int16_t current_b_reference_counts)
{
    controller->current_a_reference_counts =
        current_a_reference_counts;
    controller->current_b_reference_counts =
        current_b_reference_counts;
}

static alignment_controller_event_t fail(
    alignment_controller_t* controller,
    alignment_controller_result_t result,
    uint32_t now_millis)
{
    controller->status.state = ALIGNMENT_CONTROLLER_STATE_FAILED;
    controller->status.result = result;
    controller->status.elapsed_millis =
        now_millis - controller->operation_start_millis;
    set_reference(controller, 0, 0);
    return ALIGNMENT_CONTROLLER_EVENT_FAILED;
}

static bool current_tracks_reference(
    const alignment_controller_t* controller,
    int16_t current_a_measured_counts,
    int16_t current_b_measured_counts)
{
    return (absolute_i32(
                (int32_t)current_a_measured_counts -
                controller->current_a_reference_counts) <=
            (int32_t)controller->config.maximum_current_error_counts) &&
           (absolute_i32(
                (int32_t)current_b_measured_counts -
                controller->current_b_reference_counts) <=
            (int32_t)controller->config.maximum_current_error_counts);
}

static void begin_sampling(alignment_controller_t* controller,
                           alignment_controller_state_t state,
                           uint32_t now_millis)
{
    controller->status.state = state;
    controller->stage_start_millis = now_millis;
    controller->status.active_sample_count = 0u;
    sampler_reset(&controller->sampler);
}

bool alignment_controller_config_is_valid(
    const alignment_controller_config_t* config)
{
    uint64_t required_duration;

    if ((config == NULL) ||
        (config->settle_duration_millis == 0u) ||
        (config->sample_duration_millis == 0u) ||
        (config->maximum_duration_millis == 0u) ||
        (config->minimum_sample_count == 0u) ||
        (config->maximum_sample_span_counts == 0u) ||
        (config->maximum_closure_error_counts == 0u) ||
        (config->maximum_current_error_counts == 0u))
    {
        return false;
    }
    required_duration = 3u *
        ((uint64_t)config->settle_duration_millis +
         (uint64_t)config->sample_duration_millis);
    return required_duration <= config->maximum_duration_millis;
}

bool alignment_controller_init(
    alignment_controller_t* controller,
    const alignment_controller_config_t* config)
{
    const alignment_controller_status_t empty_status = {0};

    if ((controller == NULL) ||
        !alignment_controller_config_is_valid(config))
    {
        return false;
    }
    controller->config = *config;
    controller->status = empty_status;
    controller->status.state = ALIGNMENT_CONTROLLER_STATE_IDLE;
    sampler_reset(&controller->sampler);
    controller->alignment = NULL;
    controller->operation_start_millis = 0u;
    controller->stage_start_millis = 0u;
    set_reference(controller, 0, 0);
    controller->initialized = true;
    return true;
}

bool alignment_controller_start(
    alignment_controller_t* controller,
    motor_alignment_t* alignment,
    uint16_t alignment_current_counts,
    uint32_t now_millis)
{
    const alignment_controller_status_t empty_status = {0};

    if ((controller == NULL) || !controller->initialized ||
        (alignment == NULL) || !alignment->initialized ||
        (alignment_current_counts == 0u) ||
        (alignment_current_counts > INT16_MAX) ||
        alignment_controller_is_active(controller))
    {
        return false;
    }
    controller->status = empty_status;
    controller->status.state =
        ALIGNMENT_CONTROLLER_STATE_PHASE_ZERO_SETTLE;
    controller->status.alignment_current_counts =
        alignment_current_counts;
    controller->alignment = alignment;
    controller->operation_start_millis = now_millis;
    controller->stage_start_millis = now_millis;
    sampler_reset(&controller->sampler);
    set_reference(controller, (int16_t)alignment_current_counts, 0);
    return true;
}

alignment_controller_event_t alignment_controller_update(
    alignment_controller_t* controller,
    uint32_t now_millis,
    bool encoder_valid,
    uint16_t encoder_raw,
    int16_t current_a_measured_counts,
    int16_t current_b_measured_counts,
    bool backend_active)
{
    uint16_t mean_raw;
    uint16_t counts_per_revolution;
    uint32_t stage_elapsed;

    if ((controller == NULL) || !controller->initialized ||
        !alignment_controller_is_active(controller) ||
        (controller->alignment == NULL) ||
        !controller->alignment->initialized)
    {
        return ALIGNMENT_CONTROLLER_EVENT_NONE;
    }
    controller->status.elapsed_millis =
        now_millis - controller->operation_start_millis;
    if (controller->status.elapsed_millis >
        controller->config.maximum_duration_millis)
    {
        return fail(controller,
                    ALIGNMENT_CONTROLLER_RESULT_DEADLINE,
                    now_millis);
    }
    if (!encoder_valid)
    {
        return fail(controller,
                    ALIGNMENT_CONTROLLER_RESULT_ENCODER_INVALID,
                    now_millis);
    }
    if (!backend_active)
    {
        return fail(controller,
                    ALIGNMENT_CONTROLLER_RESULT_BACKEND_INACTIVE,
                    now_millis);
    }
    counts_per_revolution =
        controller->alignment->config.encoder_counts_per_revolution;
    if (encoder_raw >= counts_per_revolution)
    {
        return fail(controller,
                    ALIGNMENT_CONTROLLER_RESULT_ENCODER_INVALID,
                    now_millis);
    }

    stage_elapsed = now_millis - controller->stage_start_millis;
    switch (controller->status.state)
    {
        case ALIGNMENT_CONTROLLER_STATE_PHASE_ZERO_SETTLE:
            if (stage_elapsed >= controller->config.settle_duration_millis)
            {
                begin_sampling(
                    controller,
                    ALIGNMENT_CONTROLLER_STATE_PHASE_ZERO_SAMPLE,
                    now_millis);
            }
            break;
        case ALIGNMENT_CONTROLLER_STATE_PHASE_QUARTER_SETTLE:
            if (stage_elapsed >= controller->config.settle_duration_millis)
            {
                begin_sampling(
                    controller,
                    ALIGNMENT_CONTROLLER_STATE_PHASE_QUARTER_SAMPLE,
                    now_millis);
            }
            break;
        case ALIGNMENT_CONTROLLER_STATE_RETURN_ZERO_SETTLE:
            if (stage_elapsed >= controller->config.settle_duration_millis)
            {
                begin_sampling(
                    controller,
                    ALIGNMENT_CONTROLLER_STATE_RETURN_ZERO_SAMPLE,
                    now_millis);
            }
            break;
        default:
            break;
    }

    if ((controller->status.state !=
         ALIGNMENT_CONTROLLER_STATE_PHASE_ZERO_SAMPLE) &&
        (controller->status.state !=
         ALIGNMENT_CONTROLLER_STATE_PHASE_QUARTER_SAMPLE) &&
        (controller->status.state !=
         ALIGNMENT_CONTROLLER_STATE_RETURN_ZERO_SAMPLE))
    {
        return ALIGNMENT_CONTROLLER_EVENT_NONE;
    }
    if (!current_tracks_reference(
            controller,
            current_a_measured_counts,
            current_b_measured_counts))
    {
        return fail(controller,
                    ALIGNMENT_CONTROLLER_RESULT_CURRENT_TRACKING,
                    now_millis);
    }
    if (!sampler_observe(
            &controller->sampler,
            encoder_raw,
            counts_per_revolution,
            controller->config.maximum_sample_span_counts))
    {
        return fail(controller,
                    ALIGNMENT_CONTROLLER_RESULT_ENCODER_UNSTABLE,
                    now_millis);
    }
    controller->status.active_sample_count =
        controller->sampler.sample_count;
    stage_elapsed = now_millis - controller->stage_start_millis;
    if ((stage_elapsed < controller->config.sample_duration_millis) ||
        (controller->sampler.sample_count <
         controller->config.minimum_sample_count))
    {
        return ALIGNMENT_CONTROLLER_EVENT_NONE;
    }
    if (!sampler_mean(
            &controller->sampler,
            counts_per_revolution,
            &mean_raw))
    {
        return fail(controller,
                    ALIGNMENT_CONTROLLER_RESULT_ENCODER_UNSTABLE,
                    now_millis);
    }

    switch (controller->status.state)
    {
        case ALIGNMENT_CONTROLLER_STATE_PHASE_ZERO_SAMPLE:
            controller->status.phase_zero_raw = mean_raw;
            controller->status.state =
                ALIGNMENT_CONTROLLER_STATE_PHASE_QUARTER_SETTLE;
            controller->stage_start_millis = now_millis;
            set_reference(
                controller,
                0,
                (int16_t)controller->status.alignment_current_counts);
            return ALIGNMENT_CONTROLLER_EVENT_REFERENCE_CHANGED;

        case ALIGNMENT_CONTROLLER_STATE_PHASE_QUARTER_SAMPLE:
        {
            const int32_t quarter_delta = wrapped_delta(
                controller->status.phase_zero_raw,
                mean_raw,
                counts_per_revolution);
            const uint32_t quarter_magnitude = (uint32_t)
                absolute_i32(quarter_delta);
            const uint32_t denominator =
                (uint32_t)controller->alignment->config.
                    electrical_cycles_per_revolution * 4u;
            const uint32_t expected_rounded =
                ((uint32_t)counts_per_revolution +
                 (denominator / 2u)) /
                denominator;
            const int32_t quarter_error =
                (int32_t)quarter_magnitude -
                (int32_t)expected_rounded;

            controller->status.phase_quarter_raw = mean_raw;
            controller->status.observed_quarter_step_counts =
                (uint16_t)quarter_magnitude;
            controller->status.quarter_step_error_counts =
                (int16_t)quarter_error;
            controller->status.encoder_direction =
                (quarter_delta > 0) ? 1 :
                ((quarter_delta < 0) ? -1 : 0);
            controller->status.state =
                ALIGNMENT_CONTROLLER_STATE_RETURN_ZERO_SETTLE;
            controller->stage_start_millis = now_millis;
            set_reference(
                controller,
                (int16_t)controller->status.alignment_current_counts,
                0);
            return ALIGNMENT_CONTROLLER_EVENT_REFERENCE_CHANGED;
        }

        case ALIGNMENT_CONTROLLER_STATE_RETURN_ZERO_SAMPLE:
        {
            int32_t closure_error;

            controller->status.return_zero_raw = mean_raw;
            closure_error = wrapped_delta(
                controller->status.phase_zero_raw,
                controller->status.return_zero_raw,
                counts_per_revolution);
            if ((closure_error < INT16_MIN) ||
                (closure_error > INT16_MAX))
            {
                return fail(controller,
                            ALIGNMENT_CONTROLLER_RESULT_CLOSURE,
                            now_millis);
            }
            controller->status.closure_error_counts =
                (int16_t)closure_error;
            if (absolute_i32(closure_error) >
                (int32_t)controller->config.
                    maximum_closure_error_counts)
            {
                return fail(controller,
                            ALIGNMENT_CONTROLLER_RESULT_CLOSURE,
                            now_millis);
            }
            if (!motor_alignment_calibrate(
                    controller->alignment,
                    controller->status.phase_zero_raw,
                    controller->status.phase_quarter_raw))
            {
                return fail(controller,
                            ALIGNMENT_CONTROLLER_RESULT_GEOMETRY,
                            now_millis);
            }
            controller->status.state =
                ALIGNMENT_CONTROLLER_STATE_COMPLETE;
            controller->status.result =
                ALIGNMENT_CONTROLLER_RESULT_SUCCESS;
            controller->status.elapsed_millis =
                now_millis - controller->operation_start_millis;
            set_reference(controller, 0, 0);
            return ALIGNMENT_CONTROLLER_EVENT_COMPLETED;
        }

        default:
            return fail(controller,
                        ALIGNMENT_CONTROLLER_RESULT_ENCODER_UNSTABLE,
                        now_millis);
    }
}

void alignment_controller_abort(alignment_controller_t* controller,
                                uint32_t now_millis)
{
    if ((controller == NULL) || !controller->initialized ||
        !alignment_controller_is_active(controller))
    {
        return;
    }
    controller->status.state = ALIGNMENT_CONTROLLER_STATE_ABORTED;
    controller->status.result = ALIGNMENT_CONTROLLER_RESULT_ABORTED;
    controller->status.elapsed_millis =
        now_millis - controller->operation_start_millis;
    set_reference(controller, 0, 0);
}

bool alignment_controller_is_active(
    const alignment_controller_t* controller)
{
    return (controller != NULL) && controller->initialized &&
           state_is_active(controller->status.state);
}

bool alignment_controller_get_reference_counts(
    const alignment_controller_t* controller,
    int16_t* current_a_reference_counts,
    int16_t* current_b_reference_counts)
{
    if ((controller == NULL) || !controller->initialized ||
        (current_a_reference_counts == NULL) ||
        (current_b_reference_counts == NULL))
    {
        return false;
    }
    *current_a_reference_counts =
        controller->current_a_reference_counts;
    *current_b_reference_counts =
        controller->current_b_reference_counts;
    return true;
}

void alignment_controller_get_status(
    const alignment_controller_t* controller,
    alignment_controller_status_t* status)
{
    const alignment_controller_status_t empty_status = {0};

    if (status == NULL)
    {
        return;
    }
    *status = ((controller != NULL) && controller->initialized) ?
        controller->status : empty_status;
}
