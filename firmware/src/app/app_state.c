#include "mks57d/app_state.h"

static bool app_state_is_valid(app_state_t state)
{
    switch (state)
    {
        case APP_STATE_RESET_SAFE:
        case APP_STATE_DIAGNOSTIC:
        case APP_STATE_READY:
        case APP_STATE_ALIGN:
        case APP_STATE_RUN:
        case APP_STATE_FAULT:
            return true;
        default:
            return false;
    }
}

static bool app_supervisor_invariants_hold(
    const app_supervisor_t* supervisor)
{
    if ((supervisor == 0) || !app_state_is_valid(supervisor->state))
    {
        return false;
    }

    switch (supervisor->state)
    {
        case APP_STATE_RESET_SAFE:
        case APP_STATE_DIAGNOSTIC:
        case APP_STATE_READY:
        case APP_STATE_FAULT:
            return supervisor->authority == APP_AUTHORITY_NONE;
        case APP_STATE_ALIGN:
            return supervisor->authority == APP_AUTHORITY_MOTION;
        case APP_STATE_RUN:
            return (supervisor->authority == APP_AUTHORITY_DIAGNOSTIC) ||
                   (supervisor->authority == APP_AUTHORITY_MOTION);
        default:
            return false;
    }
}

static void app_supervisor_enter(app_supervisor_t* supervisor,
                                 app_state_t state,
                                 app_authority_t authority)
{
    supervisor->state = state;
    supervisor->authority = authority;
}

bool app_supervisor_init(app_supervisor_t* supervisor)
{
    if (supervisor == 0)
    {
        return false;
    }

    app_supervisor_enter(supervisor,
                         APP_STATE_RESET_SAFE,
                         APP_AUTHORITY_NONE);
    supervisor->initialized = true;
    return true;
}

bool app_supervisor_handle_event(app_supervisor_t* supervisor,
                                 app_event_t event,
                                 app_transition_context_t context)
{
    if ((supervisor == 0) || !supervisor->initialized)
    {
        return false;
    }

    if (!app_supervisor_invariants_hold(supervisor))
    {
        app_supervisor_enter(supervisor, APP_STATE_FAULT, APP_AUTHORITY_NONE);
        return false;
    }

    if (event == APP_EVENT_FAULT_DETECTED)
    {
        app_supervisor_enter(supervisor, APP_STATE_FAULT, APP_AUTHORITY_NONE);
        return true;
    }

    switch (supervisor->state)
    {
        case APP_STATE_RESET_SAFE:
            if (event == APP_EVENT_PASSIVE_INIT_COMPLETE)
            {
                app_supervisor_enter(supervisor,
                                     APP_STATE_DIAGNOSTIC,
                                     APP_AUTHORITY_NONE);
                return true;
            }
            break;

        case APP_STATE_DIAGNOSTIC:
            if (event == APP_EVENT_READINESS_LOST)
            {
                return true;
            }
            if ((event == APP_EVENT_READINESS_CONFIRMED) &&
                context.safe_to_energize)
            {
                app_supervisor_enter(supervisor,
                                     APP_STATE_READY,
                                     APP_AUTHORITY_NONE);
                return true;
            }
            break;

        case APP_STATE_READY:
            if (event == APP_EVENT_READINESS_LOST)
            {
                app_supervisor_enter(supervisor,
                                     APP_STATE_DIAGNOSTIC,
                                     APP_AUTHORITY_NONE);
                return true;
            }
            if (!context.safe_to_energize)
            {
                break;
            }
            if (event == APP_EVENT_DIAGNOSTIC_OPERATION_REQUESTED)
            {
                app_supervisor_enter(supervisor,
                                     APP_STATE_RUN,
                                     APP_AUTHORITY_DIAGNOSTIC);
                return true;
            }
            if (event == APP_EVENT_ALIGNMENT_REQUESTED)
            {
                app_supervisor_enter(supervisor,
                                     APP_STATE_ALIGN,
                                     APP_AUTHORITY_MOTION);
                return true;
            }
            if (event == APP_EVENT_MOTION_RUN_REQUESTED)
            {
                app_supervisor_enter(supervisor,
                                     APP_STATE_RUN,
                                     APP_AUTHORITY_MOTION);
                return true;
            }
            break;

        case APP_STATE_ALIGN:
            if (event == APP_EVENT_READINESS_LOST)
            {
                app_supervisor_enter(supervisor,
                                     APP_STATE_FAULT,
                                     APP_AUTHORITY_NONE);
                return true;
            }
            if ((event == APP_EVENT_ALIGNMENT_COMPLETED) ||
                (event == APP_EVENT_AUTHORITY_RELEASED))
            {
                app_supervisor_enter(supervisor,
                                     APP_STATE_READY,
                                     APP_AUTHORITY_NONE);
                return true;
            }
            break;

        case APP_STATE_RUN:
            if (event == APP_EVENT_READINESS_LOST)
            {
                app_supervisor_enter(supervisor,
                                     APP_STATE_FAULT,
                                     APP_AUTHORITY_NONE);
                return true;
            }
            if (event == APP_EVENT_AUTHORITY_RELEASED)
            {
                app_supervisor_enter(supervisor,
                                     APP_STATE_READY,
                                     APP_AUTHORITY_NONE);
                return true;
            }
            break;

        case APP_STATE_FAULT:
            if ((event == APP_EVENT_FAULT_ACKNOWLEDGED) &&
                context.safe_to_recover)
            {
                app_supervisor_enter(supervisor,
                                     APP_STATE_DIAGNOSTIC,
                                     APP_AUTHORITY_NONE);
                return true;
            }
            break;

        default:
            app_supervisor_enter(supervisor,
                                 APP_STATE_FAULT,
                                 APP_AUTHORITY_NONE);
            break;
    }

    return false;
}

bool app_supervisor_bridge_authorized(const app_supervisor_t* supervisor)
{
    if ((supervisor == 0) || !supervisor->initialized ||
        !app_supervisor_invariants_hold(supervisor))
    {
        return false;
    }

    if (supervisor->state == APP_STATE_ALIGN)
    {
        return supervisor->authority == APP_AUTHORITY_MOTION;
    }

    if (supervisor->state == APP_STATE_RUN)
    {
        return (supervisor->authority == APP_AUTHORITY_DIAGNOSTIC) ||
               (supervisor->authority == APP_AUTHORITY_MOTION);
    }

    return false;
}

bool app_supervisor_foreground_service_allowed(
    const app_supervisor_t* supervisor)
{
    return (supervisor != 0) && supervisor->initialized &&
           app_supervisor_invariants_hold(supervisor) &&
           (supervisor->state != APP_STATE_RESET_SAFE);
}
