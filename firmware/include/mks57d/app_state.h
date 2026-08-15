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
    APP_EVENT_FAULT_DETECTED,
    APP_EVENT_FAULT_ACKNOWLEDGED
} app_event_t;

typedef struct
{
    bool safe_to_recover;
} app_transition_context_t;

app_state_t app_state_transition(app_state_t current,
                                 app_event_t event,
                                 app_transition_context_t context);

#endif
