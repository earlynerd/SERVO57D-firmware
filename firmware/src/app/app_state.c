#include "mks57d/app_state.h"

app_state_t app_state_transition(app_state_t current,
                                 app_event_t event,
                                 app_transition_context_t context)
{
    if (event == APP_EVENT_FAULT_DETECTED)
    {
        return APP_STATE_FAULT;
    }

    if ((current == APP_STATE_RESET_SAFE) &&
        (event == APP_EVENT_PASSIVE_INIT_COMPLETE))
    {
        return APP_STATE_DIAGNOSTIC;
    }

    if ((current == APP_STATE_FAULT) &&
        (event == APP_EVENT_FAULT_ACKNOWLEDGED) &&
        context.safe_to_recover)
    {
        return APP_STATE_DIAGNOSTIC;
    }

    return current;
}
