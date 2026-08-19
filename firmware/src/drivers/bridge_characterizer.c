#include "mks57d/bridge_characterizer.h"

#include <stddef.h>

#include "mks57d/user_inputs.h"

static bool levels_are_valid(uint32_t levels)
{
    return (levels & ~USER_INPUT_MASK) == 0u;
}

bool bridge_characterizer_init(bridge_characterizer_t* characterizer,
                               uint32_t raw_levels,
                               uint32_t debounced_levels)
{
    if ((characterizer == NULL) || !levels_are_valid(raw_levels) ||
        !levels_are_valid(debounced_levels))
    {
        return false;
    }

    characterizer->selected_leg = BRIDGE_CHARACTERIZER_LEG_A1;
    characterizer->previous_debounced_levels = debounced_levels;
    characterizer->active = false;
    characterizer->enter_release_seen =
        (raw_levels & USER_INPUT_KEY_ENTER) != 0u;
    characterizer->initialized = true;
    return true;
}

bool bridge_characterizer_update(bridge_characterizer_t* characterizer,
                                 uint32_t raw_levels,
                                 uint32_t debounced_levels)
{
    const bool raw_enter_high =
        (raw_levels & USER_INPUT_KEY_ENTER) != 0u;
    const bool raw_menu_high =
        (raw_levels & USER_INPUT_KEY_MENU) != 0u;
    const bool next_pressed =
        ((characterizer != NULL) && characterizer->initialized) &&
        ((characterizer->previous_debounced_levels &
          USER_INPUT_KEY_NEXT) != 0u) &&
        ((debounced_levels & USER_INPUT_KEY_NEXT) == 0u);
    const bool enter_pressed =
        ((characterizer != NULL) && characterizer->initialized) &&
        ((characterizer->previous_debounced_levels &
          USER_INPUT_KEY_ENTER) != 0u) &&
        ((debounced_levels & USER_INPUT_KEY_ENTER) == 0u);
    bool changed = false;

    if ((characterizer == NULL) || !characterizer->initialized ||
        !levels_are_valid(raw_levels) ||
        !levels_are_valid(debounced_levels))
    {
        return false;
    }

    if (!raw_menu_high || (characterizer->active && raw_enter_high))
    {
        if (characterizer->active)
        {
            characterizer->active = false;
            changed = true;
        }
    }

    if (raw_enter_high)
    {
        characterizer->enter_release_seen = true;
    }

    if (!characterizer->active && raw_menu_high)
    {
        if (next_pressed)
        {
            characterizer->selected_leg =
                (bridge_characterizer_leg_t)(
                    ((uint32_t)characterizer->selected_leg + 1u) %
                    (uint32_t)BRIDGE_CHARACTERIZER_LEG_COUNT);
            changed = true;
        }

        if (characterizer->enter_release_seen && enter_pressed &&
            !raw_enter_high)
        {
            characterizer->active = true;
            characterizer->enter_release_seen = false;
            changed = true;
        }
    }

    characterizer->previous_debounced_levels = debounced_levels;
    return changed;
}

void bridge_characterizer_stop(
    bridge_characterizer_t* characterizer)
{
    if (characterizer == NULL)
    {
        return;
    }

    characterizer->active = false;
}
