#include "mks57d/user_inputs.h"

#include <stddef.h>
#include <string.h>

static const uint32_t USER_INPUT_BITS[USER_INPUT_COUNT] = {
    USER_INPUT_BUTTON_LEFT,
    USER_INPUT_BUTTON_CENTER,
    USER_INPUT_BUTTON_RIGHT,
    USER_INPUT_M_IN1,
    USER_INPUT_M_IN2,
    USER_INPUT_STEP,
    USER_INPUT_DIRECTION,
    USER_INPUT_ENABLE,
};

bool user_inputs_debouncer_init(user_inputs_debouncer_t* debouncer,
                                uint32_t raw_levels)
{
    if ((debouncer == NULL) || ((raw_levels & ~USER_INPUT_MASK) != 0u))
    {
        return false;
    }

    debouncer->stable_levels = raw_levels;
    memset(debouncer->transition_counts,
           0,
           sizeof(debouncer->transition_counts));
    debouncer->initialized = true;
    return true;
}

bool user_inputs_debouncer_update(user_inputs_debouncer_t* debouncer,
                                  uint32_t raw_levels)
{
    bool changed = false;
    size_t index;

    if ((debouncer == NULL) || !debouncer->initialized ||
        ((raw_levels & ~USER_INPUT_MASK) != 0u))
    {
        return false;
    }

    for (index = 0u; index < USER_INPUT_COUNT; ++index)
    {
        const uint32_t bit = USER_INPUT_BITS[index];
        const bool raw_high = (raw_levels & bit) != 0u;
        const bool stable_high = (debouncer->stable_levels & bit) != 0u;

        if (raw_high == stable_high)
        {
            debouncer->transition_counts[index] = 0u;
            continue;
        }

        ++debouncer->transition_counts[index];
        if (debouncer->transition_counts[index] < USER_INPUT_DEBOUNCE_SAMPLES)
        {
            continue;
        }

        if (raw_high)
        {
            debouncer->stable_levels |= bit;
        }
        else
        {
            debouncer->stable_levels &= ~bit;
        }
        debouncer->transition_counts[index] = 0u;
        changed = true;
    }

    return changed;
}

uint32_t user_inputs_debounced_levels(
    const user_inputs_debouncer_t* debouncer)
{
    if ((debouncer == NULL) || !debouncer->initialized)
    {
        return USER_INPUT_MASK;
    }
    return debouncer->stable_levels;
}
