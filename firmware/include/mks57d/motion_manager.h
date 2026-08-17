#ifndef MKS57D_MOTION_MANAGER_H
#define MKS57D_MOTION_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    MOTION_SOURCE_NONE = 0,
    MOTION_SOURCE_NATIVE,
    MOTION_SOURCE_MODBUS,
    MOTION_SOURCE_MAKERBASE,
    MOTION_SOURCE_STEP_DIRECTION,
    MOTION_SOURCE_LOCAL
} motion_source_t;

enum
{
    MOTION_REQUEST_HISTORY_CAPACITY = 8u,
    MOTION_SOURCE_MASK_NATIVE = 1u << MOTION_SOURCE_NATIVE,
    MOTION_SOURCE_MASK_MODBUS = 1u << MOTION_SOURCE_MODBUS,
    MOTION_SOURCE_MASK_MAKERBASE = 1u << MOTION_SOURCE_MAKERBASE,
    MOTION_SOURCE_MASK_STEP_DIRECTION = 1u << MOTION_SOURCE_STEP_DIRECTION,
    MOTION_SOURCE_MASK_LOCAL = 1u << MOTION_SOURCE_LOCAL,
    MOTION_SOURCE_MASK_ALL = MOTION_SOURCE_MASK_NATIVE |
                             MOTION_SOURCE_MASK_MODBUS |
                             MOTION_SOURCE_MASK_MAKERBASE |
                             MOTION_SOURCE_MASK_STEP_DIRECTION |
                             MOTION_SOURCE_MASK_LOCAL
};

typedef enum
{
    MOTION_COMMAND_ENABLE = 0,
    MOTION_COMMAND_DISABLE,
    MOTION_COMMAND_STOP,
    MOTION_COMMAND_KEEPALIVE,
    MOTION_COMMAND_MOVE_ABSOLUTE,
    MOTION_COMMAND_MOVE_RELATIVE
} motion_command_kind_t;

typedef enum
{
    MOTION_STATE_DISABLED = 0,
    MOTION_STATE_READY,
    MOTION_STATE_MOVING,
    MOTION_STATE_STOPPING,
    MOTION_STATE_FAULT
} motion_state_t;

typedef enum
{
    MOTION_SUBMIT_ACCEPTED = 0,
    MOTION_SUBMIT_DUPLICATE,
    MOTION_SUBMIT_INVALID_ARGUMENT,
    MOTION_SUBMIT_SOURCE_DISABLED,
    MOTION_SUBMIT_BUSY,
    MOTION_SUBMIT_NOT_ENABLED,
    MOTION_SUBMIT_CONFLICT,
    MOTION_SUBMIT_FAULTED
} motion_submit_status_t;

typedef enum
{
    MOTION_COMPLETION_NONE = 0,
    MOTION_COMPLETION_ACTIVE,
    MOTION_COMPLETION_COMPLETED,
    MOTION_COMPLETION_ABORTED_STOP,
    MOTION_COMPLETION_ABORTED_DISABLE,
    MOTION_COMPLETION_ABORTED_LEASE,
    MOTION_COMPLETION_ABORTED_SUPERSEDED,
    MOTION_COMPLETION_FAULTED
} motion_completion_t;

typedef enum
{
    MOTION_ACTION_NONE = 0,
    MOTION_ACTION_ENABLE,
    MOTION_ACTION_SET_POSITION_TARGET,
    MOTION_ACTION_REQUEST_CONTROLLED_STOP,
    MOTION_ACTION_DISABLE
} motion_action_kind_t;

typedef struct
{
    uint32_t remote_lease_timeout_us;
    uint32_t allowed_motion_sources;
} motion_manager_config_t;

typedef struct
{
    motion_source_t source;
    uint32_t command_id;
    motion_command_kind_t kind;
    float position_revolutions;
} motion_request_t;

typedef struct
{
    motion_action_kind_t kind;
    uint32_t command_id;
    float position_revolutions;
} motion_action_t;

typedef struct
{
    motion_state_t state;
    motion_source_t authority;
    motion_source_t active_command_source;
    uint32_t active_command_id;
    motion_completion_t active_completion;
    motion_source_t last_command_source;
    uint32_t last_command_id;
    motion_completion_t last_completion;
    motion_source_t previous_command_source;
    uint32_t previous_command_id;
    motion_completion_t previous_completion;
    float target_position_revolutions;
    bool lease_active;
} motion_manager_status_t;

typedef struct
{
    motion_manager_config_t config;
    motion_state_t state;
    motion_source_t authority;
    motion_request_t recent_requests[MOTION_REQUEST_HISTORY_CAPACITY];
    bool recent_request_valid[MOTION_REQUEST_HISTORY_CAPACITY];
    uint8_t next_request_index;
    motion_source_t active_command_source;
    uint32_t active_command_id;
    motion_completion_t active_completion;
    bool active_command_valid;
    motion_source_t last_command_source;
    uint32_t last_command_id;
    motion_completion_t last_completion;
    bool last_completion_valid;
    motion_source_t previous_command_source;
    uint32_t previous_command_id;
    motion_completion_t previous_completion;
    bool previous_completion_valid;
    float target_position_revolutions;
    uint32_t lease_deadline_us;
    bool lease_active;
    bool initialized;
} motion_manager_t;

bool motion_manager_config_is_valid(const motion_manager_config_t* config);
bool motion_manager_init(motion_manager_t* manager,
                         const motion_manager_config_t* config,
                         float initial_position_revolutions);
motion_submit_status_t motion_manager_submit(
    motion_manager_t* manager,
    const motion_request_t* request,
    uint32_t timestamp_us,
    float current_position_revolutions,
    motion_action_t* action);
motion_submit_status_t motion_manager_update_stream_target(
    motion_manager_t* manager,
    motion_source_t source,
    float target_position_revolutions,
    motion_action_t* action);
bool motion_manager_poll(motion_manager_t* manager,
                         uint32_t timestamp_us,
                         bool trajectory_settled,
                         bool servo_faulted,
                         motion_action_t* action);
bool motion_manager_clear_fault(motion_manager_t* manager,
                                bool safe_to_recover,
                                float current_position_revolutions);
void motion_manager_get_status(const motion_manager_t* manager,
                               motion_manager_status_t* status);

#endif
