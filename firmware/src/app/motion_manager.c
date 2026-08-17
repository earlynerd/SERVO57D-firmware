#include "mks57d/motion_manager.h"

#include <limits.h>
#include <math.h>
#include <stddef.h>

static void action_reset(motion_action_t* action)
{
    action->kind = MOTION_ACTION_NONE;
    action->command_id = 0u;
    action->position_revolutions = 0.0f;
}

static bool source_is_valid(motion_source_t source)
{
    return (source > MOTION_SOURCE_NONE) &&
           (source <= MOTION_SOURCE_LOCAL);
}

static bool source_uses_lease(motion_source_t source)
{
    return (source == MOTION_SOURCE_NATIVE) ||
           (source == MOTION_SOURCE_MODBUS) ||
           (source == MOTION_SOURCE_MAKERBASE);
}

static bool source_is_allowed(const motion_manager_t* manager,
                              motion_source_t source)
{
    return (manager->config.allowed_motion_sources &
            (1u << (unsigned int)source)) != 0u;
}

static bool command_is_move(motion_command_kind_t kind)
{
    return (kind == MOTION_COMMAND_MOVE_ABSOLUTE) ||
           (kind == MOTION_COMMAND_MOVE_RELATIVE);
}

static bool request_is_valid(const motion_request_t* request)
{
    return (request != NULL) && source_is_valid(request->source) &&
           ((unsigned int)request->kind <=
            (unsigned int)MOTION_COMMAND_MOVE_RELATIVE) &&
           (!command_is_move(request->kind) ||
            isfinite(request->position_revolutions));
}

static bool requests_match(const motion_request_t* left,
                           const motion_request_t* right)
{
    return (left->source == right->source) &&
           (left->command_id == right->command_id) &&
           (left->kind == right->kind) &&
           (!command_is_move(left->kind) ||
            (left->position_revolutions == right->position_revolutions));
}

static bool request_identity_matches(const motion_request_t* left,
                                     const motion_request_t* right)
{
    return (left->source == right->source) &&
           (left->command_id == right->command_id);
}

static bool find_request(const motion_manager_t* manager,
                         const motion_request_t* request,
                         bool* exact_match)
{
    unsigned int index;

    for (index = 0u; index < MOTION_REQUEST_HISTORY_CAPACITY; ++index)
    {
        if (manager->recent_request_valid[index] &&
            request_identity_matches(&manager->recent_requests[index],
                                     request))
        {
            *exact_match = requests_match(&manager->recent_requests[index],
                                          request);
            return true;
        }
    }
    return false;
}

static void remember_request(motion_manager_t* manager,
                             const motion_request_t* request)
{
    manager->recent_requests[manager->next_request_index] = *request;
    manager->recent_request_valid[manager->next_request_index] = true;
    manager->next_request_index = (uint8_t)(
        (manager->next_request_index + 1u) %
        MOTION_REQUEST_HISTORY_CAPACITY);
}

static void record_completion(motion_manager_t* manager,
                              motion_source_t source,
                              uint32_t command_id,
                              motion_completion_t completion)
{
    if (manager->last_completion_valid)
    {
        manager->previous_command_source = manager->last_command_source;
        manager->previous_command_id = manager->last_command_id;
        manager->previous_completion = manager->last_completion;
        manager->previous_completion_valid = true;
    }
    manager->last_command_source = source;
    manager->last_command_id = command_id;
    manager->last_completion = completion;
    manager->last_completion_valid = true;
}

static void record_active_completion(motion_manager_t* manager,
                                     motion_completion_t completion)
{
    if (!manager->active_command_valid)
    {
        return;
    }

    record_completion(manager,
                      manager->active_command_source,
                      manager->active_command_id,
                      completion);
    manager->active_command_source = MOTION_SOURCE_NONE;
    manager->active_command_id = 0u;
    manager->active_completion = MOTION_COMPLETION_NONE;
    manager->active_command_valid = false;
}

static void record_immediate_completion(motion_manager_t* manager,
                                        motion_source_t source,
                                        uint32_t command_id)
{
    record_completion(manager,
                      source,
                      command_id,
                      MOTION_COMPLETION_COMPLETED);
}

static void begin_active_command(motion_manager_t* manager,
                                 motion_source_t source,
                                 uint32_t command_id)
{
    manager->active_command_source = source;
    manager->active_command_id = command_id;
    manager->active_completion = MOTION_COMPLETION_ACTIVE;
    manager->active_command_valid = true;
}

static void refresh_lease(motion_manager_t* manager,
                          motion_source_t source,
                          uint32_t timestamp_us)
{
    if (source_uses_lease(source))
    {
        manager->lease_deadline_us =
            timestamp_us + manager->config.remote_lease_timeout_us;
        manager->lease_active = true;
    }
    else
    {
        manager->lease_active = false;
    }
}

static bool deadline_reached(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

bool motion_manager_config_is_valid(const motion_manager_config_t* config)
{
    return (config != NULL) &&
           (config->remote_lease_timeout_us != 0u) &&
           (config->remote_lease_timeout_us <= (uint32_t)INT32_MAX) &&
           (config->allowed_motion_sources != 0u) &&
           ((config->allowed_motion_sources & ~MOTION_SOURCE_MASK_ALL) == 0u);
}

bool motion_manager_init(motion_manager_t* manager,
                         const motion_manager_config_t* config,
                         float initial_position_revolutions)
{
    unsigned int index;

    if ((manager == NULL) ||
        !motion_manager_config_is_valid(config) ||
        !isfinite(initial_position_revolutions))
    {
        return false;
    }

    manager->config = *config;
    manager->state = MOTION_STATE_DISABLED;
    manager->authority = MOTION_SOURCE_NONE;
    for (index = 0u; index < MOTION_REQUEST_HISTORY_CAPACITY; ++index)
    {
        manager->recent_request_valid[index] = false;
    }
    manager->next_request_index = 0u;
    manager->active_command_source = MOTION_SOURCE_NONE;
    manager->active_command_id = 0u;
    manager->active_completion = MOTION_COMPLETION_NONE;
    manager->active_command_valid = false;
    manager->last_command_source = MOTION_SOURCE_NONE;
    manager->last_command_id = 0u;
    manager->last_completion = MOTION_COMPLETION_NONE;
    manager->last_completion_valid = false;
    manager->previous_command_source = MOTION_SOURCE_NONE;
    manager->previous_command_id = 0u;
    manager->previous_completion = MOTION_COMPLETION_NONE;
    manager->previous_completion_valid = false;
    manager->target_position_revolutions = initial_position_revolutions;
    manager->lease_deadline_us = 0u;
    manager->lease_active = false;
    manager->initialized = true;
    return true;
}

motion_submit_status_t motion_manager_submit(
    motion_manager_t* manager,
    const motion_request_t* request,
    uint32_t timestamp_us,
    float current_position_revolutions,
    motion_action_t* action)
{
    float target_position;
    bool exact_match;

    if ((manager == NULL) || (action == NULL) || !manager->initialized ||
        !request_is_valid(request) ||
        !isfinite(current_position_revolutions))
    {
        return MOTION_SUBMIT_INVALID_ARGUMENT;
    }
    action_reset(action);

    if (find_request(manager, request, &exact_match))
    {
        return exact_match ?
            MOTION_SUBMIT_DUPLICATE : MOTION_SUBMIT_CONFLICT;
    }

    if (manager->state == MOTION_STATE_FAULT)
    {
        if (request->kind != MOTION_COMMAND_DISABLE)
        {
            return MOTION_SUBMIT_FAULTED;
        }
        remember_request(manager, request);
        record_immediate_completion(manager,
                                    request->source,
                                    request->command_id);
        action->kind = MOTION_ACTION_DISABLE;
        action->command_id = request->command_id;
        return MOTION_SUBMIT_ACCEPTED;
    }

    if (request->kind == MOTION_COMMAND_DISABLE)
    {
        record_active_completion(manager,
                                 MOTION_COMPLETION_ABORTED_DISABLE);
        remember_request(manager, request);
        record_immediate_completion(manager,
                                    request->source,
                                    request->command_id);
        manager->state = MOTION_STATE_DISABLED;
        manager->authority = MOTION_SOURCE_NONE;
        manager->target_position_revolutions =
            current_position_revolutions;
        manager->lease_active = false;
        action->kind = MOTION_ACTION_DISABLE;
        action->command_id = request->command_id;
        return MOTION_SUBMIT_ACCEPTED;
    }

    if (request->kind == MOTION_COMMAND_STOP)
    {
        remember_request(manager, request);
        manager->lease_active = false;
        if (manager->state == MOTION_STATE_DISABLED)
        {
            record_immediate_completion(manager,
                                        request->source,
                                        request->command_id);
            return MOTION_SUBMIT_ACCEPTED;
        }

        record_active_completion(manager,
                                 MOTION_COMPLETION_ABORTED_STOP);
        begin_active_command(manager,
                             request->source,
                             request->command_id);
        manager->state = MOTION_STATE_STOPPING;
        action->kind = MOTION_ACTION_REQUEST_CONTROLLED_STOP;
        action->command_id = request->command_id;
        return MOTION_SUBMIT_ACCEPTED;
    }

    if (!source_is_allowed(manager, request->source))
    {
        return MOTION_SUBMIT_SOURCE_DISABLED;
    }

    if ((manager->authority != MOTION_SOURCE_NONE) &&
        (manager->authority != request->source))
    {
        return MOTION_SUBMIT_BUSY;
    }

    if (request->kind == MOTION_COMMAND_KEEPALIVE)
    {
        if (!source_uses_lease(request->source))
        {
            return MOTION_SUBMIT_INVALID_ARGUMENT;
        }
        if (manager->state == MOTION_STATE_DISABLED)
        {
            return MOTION_SUBMIT_NOT_ENABLED;
        }
        if ((manager->state != MOTION_STATE_READY) &&
            (manager->state != MOTION_STATE_MOVING))
        {
            return MOTION_SUBMIT_BUSY;
        }
        remember_request(manager, request);
        record_immediate_completion(manager,
                                    request->source,
                                    request->command_id);
        refresh_lease(manager, request->source, timestamp_us);
        return MOTION_SUBMIT_ACCEPTED;
    }

    if (request->kind == MOTION_COMMAND_ENABLE)
    {
        if (manager->state == MOTION_STATE_STOPPING)
        {
            return MOTION_SUBMIT_BUSY;
        }
        if (manager->state == MOTION_STATE_DISABLED)
        {
            manager->state = MOTION_STATE_READY;
            manager->authority = request->source;
            manager->target_position_revolutions =
                current_position_revolutions;
            action->kind = MOTION_ACTION_ENABLE;
            action->command_id = request->command_id;
        }
        remember_request(manager, request);
        record_immediate_completion(manager,
                                    request->source,
                                    request->command_id);
        refresh_lease(manager, request->source, timestamp_us);
        return MOTION_SUBMIT_ACCEPTED;
    }

    if (manager->state == MOTION_STATE_DISABLED)
    {
        return MOTION_SUBMIT_NOT_ENABLED;
    }
    if ((manager->state != MOTION_STATE_READY) &&
        (manager->state != MOTION_STATE_MOVING))
    {
        return MOTION_SUBMIT_BUSY;
    }

    target_position = (request->kind == MOTION_COMMAND_MOVE_ABSOLUTE) ?
        request->position_revolutions :
        current_position_revolutions + request->position_revolutions;
    if (!isfinite(target_position))
    {
        return MOTION_SUBMIT_INVALID_ARGUMENT;
    }

    if (manager->state == MOTION_STATE_MOVING)
    {
        record_active_completion(
            manager,
            MOTION_COMPLETION_ABORTED_SUPERSEDED);
    }
    remember_request(manager, request);
    begin_active_command(manager,
                         request->source,
                         request->command_id);
    manager->state = MOTION_STATE_MOVING;
    manager->authority = request->source;
    manager->target_position_revolutions = target_position;
    refresh_lease(manager, request->source, timestamp_us);
    action->kind = MOTION_ACTION_SET_POSITION_TARGET;
    action->command_id = request->command_id;
    action->position_revolutions = target_position;
    return MOTION_SUBMIT_ACCEPTED;
}

motion_submit_status_t motion_manager_update_stream_target(
    motion_manager_t* manager,
    motion_source_t source,
    float target_position_revolutions,
    motion_action_t* action)
{
    if ((manager == NULL) || (action == NULL) || !manager->initialized ||
        !source_is_valid(source) || !isfinite(target_position_revolutions))
    {
        return MOTION_SUBMIT_INVALID_ARGUMENT;
    }
    action_reset(action);
    if (manager->state == MOTION_STATE_FAULT)
    {
        return MOTION_SUBMIT_FAULTED;
    }
    if (!source_is_allowed(manager, source))
    {
        return MOTION_SUBMIT_SOURCE_DISABLED;
    }
    if (manager->state == MOTION_STATE_DISABLED)
    {
        return MOTION_SUBMIT_NOT_ENABLED;
    }
    if ((manager->authority != source) ||
        (manager->state == MOTION_STATE_STOPPING))
    {
        return MOTION_SUBMIT_BUSY;
    }

    record_active_completion(manager,
                             MOTION_COMPLETION_ABORTED_SUPERSEDED);
    manager->state = MOTION_STATE_MOVING;
    manager->target_position_revolutions = target_position_revolutions;
    action->kind = MOTION_ACTION_SET_POSITION_TARGET;
    action->position_revolutions = target_position_revolutions;
    return MOTION_SUBMIT_ACCEPTED;
}

bool motion_manager_poll(motion_manager_t* manager,
                         uint32_t timestamp_us,
                         bool trajectory_settled,
                         bool servo_faulted,
                         motion_action_t* action)
{
    if ((manager == NULL) || (action == NULL) || !manager->initialized)
    {
        return false;
    }
    action_reset(action);

    if (servo_faulted && (manager->state != MOTION_STATE_FAULT))
    {
        record_active_completion(manager, MOTION_COMPLETION_FAULTED);
        manager->state = MOTION_STATE_FAULT;
        manager->authority = MOTION_SOURCE_NONE;
        manager->lease_active = false;
        action->kind = MOTION_ACTION_DISABLE;
        return true;
    }
    if (manager->state == MOTION_STATE_FAULT)
    {
        return true;
    }

    if (manager->lease_active &&
        ((manager->state == MOTION_STATE_READY) ||
         (manager->state == MOTION_STATE_MOVING)) &&
        deadline_reached(timestamp_us, manager->lease_deadline_us))
    {
        record_active_completion(manager,
                                 MOTION_COMPLETION_ABORTED_LEASE);
        manager->state = MOTION_STATE_STOPPING;
        manager->lease_active = false;
        action->kind = MOTION_ACTION_REQUEST_CONTROLLED_STOP;
        return true;
    }

    if ((manager->state == MOTION_STATE_MOVING) && trajectory_settled)
    {
        record_active_completion(manager, MOTION_COMPLETION_COMPLETED);
        manager->state = MOTION_STATE_READY;
        return true;
    }

    if ((manager->state == MOTION_STATE_STOPPING) && trajectory_settled)
    {
        record_active_completion(manager, MOTION_COMPLETION_COMPLETED);
        manager->state = MOTION_STATE_DISABLED;
        manager->authority = MOTION_SOURCE_NONE;
        manager->lease_active = false;
        action->kind = MOTION_ACTION_DISABLE;
    }
    return true;
}

bool motion_manager_clear_fault(motion_manager_t* manager,
                                bool safe_to_recover,
                                float current_position_revolutions)
{
    if ((manager == NULL) || !manager->initialized ||
        !safe_to_recover || !isfinite(current_position_revolutions) ||
        (manager->state != MOTION_STATE_FAULT))
    {
        return false;
    }

    manager->state = MOTION_STATE_DISABLED;
    manager->authority = MOTION_SOURCE_NONE;
    manager->active_command_source = MOTION_SOURCE_NONE;
    manager->active_command_id = 0u;
    manager->active_completion = MOTION_COMPLETION_NONE;
    manager->active_command_valid = false;
    manager->target_position_revolutions =
        current_position_revolutions;
    manager->lease_active = false;
    return true;
}

void motion_manager_get_status(const motion_manager_t* manager,
                               motion_manager_status_t* status)
{
    if ((manager == NULL) || (status == NULL) || !manager->initialized)
    {
        return;
    }

    status->state = manager->state;
    status->authority = manager->authority;
    status->active_command_source = manager->active_command_valid ?
        manager->active_command_source : MOTION_SOURCE_NONE;
    status->active_command_id = manager->active_command_valid ?
        manager->active_command_id : 0u;
    status->active_completion = manager->active_command_valid ?
        manager->active_completion : MOTION_COMPLETION_NONE;
    status->last_command_source = manager->last_completion_valid ?
        manager->last_command_source : MOTION_SOURCE_NONE;
    status->last_command_id = manager->last_completion_valid ?
        manager->last_command_id : 0u;
    status->last_completion = manager->last_completion_valid ?
        manager->last_completion : MOTION_COMPLETION_NONE;
    status->previous_command_source = manager->previous_completion_valid ?
        manager->previous_command_source : MOTION_SOURCE_NONE;
    status->previous_command_id = manager->previous_completion_valid ?
        manager->previous_command_id : 0u;
    status->previous_completion = manager->previous_completion_valid ?
        manager->previous_completion : MOTION_COMPLETION_NONE;
    status->target_position_revolutions =
        manager->target_position_revolutions;
    status->lease_active = manager->lease_active;
}
