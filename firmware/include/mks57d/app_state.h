#ifndef MKS57D_APP_STATE_H
#define MKS57D_APP_STATE_H

#include <stdbool.h>

typedef enum
{
    APP_STATE_RESET_SAFE = 0,
    APP_STATE_DIAGNOSTIC,
    APP_STATE_READY,
    APP_STATE_ALIGN,
    APP_STATE_RUN,
    APP_STATE_FAULT
} app_state_t;

typedef enum
{
    APP_EVENT_PASSIVE_INIT_COMPLETE = 0,
    APP_EVENT_READINESS_CONFIRMED,
    APP_EVENT_READINESS_LOST,
    APP_EVENT_DIAGNOSTIC_OPERATION_REQUESTED,
    APP_EVENT_ALIGNMENT_REQUESTED,
    APP_EVENT_ALIGNMENT_COMPLETED,
    APP_EVENT_MOTION_RUN_REQUESTED,
    APP_EVENT_AUTHORITY_RELEASED,
    APP_EVENT_FAULT_DETECTED,
    APP_EVENT_FAULT_ACKNOWLEDGED
} app_event_t;

typedef enum
{
    APP_AUTHORITY_NONE = 0,
    APP_AUTHORITY_DIAGNOSTIC,
    APP_AUTHORITY_MOTION
} app_authority_t;

typedef struct
{
    bool safe_to_energize;
    bool safe_to_recover;
} app_transition_context_t;

typedef struct
{
    app_state_t state;
    app_authority_t authority;
    bool initialized;
} app_supervisor_t;

bool app_supervisor_init(app_supervisor_t* supervisor);
bool app_supervisor_handle_event(app_supervisor_t* supervisor,
                                 app_event_t event,
                                 app_transition_context_t context);
bool app_supervisor_bridge_authorized(const app_supervisor_t* supervisor);
bool app_supervisor_foreground_service_allowed(
    const app_supervisor_t* supervisor);

#endif
