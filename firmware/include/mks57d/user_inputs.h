#ifndef MKS57D_USER_INPUTS_H
#define MKS57D_USER_INPUTS_H

#include <stdbool.h>
#include <stdint.h>

enum
{
    USER_INPUT_BUTTON_CENTER = 1u << 0,
    USER_INPUT_BUTTON_RIGHT = 1u << 1,
    USER_INPUT_BUTTON_LEFT = 1u << 2,
    USER_INPUT_M_IN1 = 1u << 3,
    USER_INPUT_M_IN2 = 1u << 4,
    USER_INPUT_STEP = 1u << 5,
    USER_INPUT_DIRECTION = 1u << 6,
    USER_INPUT_ENABLE = 1u << 7,
    USER_INPUT_MASK = USER_INPUT_BUTTON_CENTER |
                      USER_INPUT_BUTTON_RIGHT |
                      USER_INPUT_BUTTON_LEFT |
                      USER_INPUT_M_IN1 |
                      USER_INPUT_M_IN2 |
                      USER_INPUT_STEP |
                      USER_INPUT_DIRECTION |
                      USER_INPUT_ENABLE,
    USER_INPUT_LOCAL_COUNT = 5u,
    USER_INPUT_COUNT = 8u,
    USER_INPUT_DEBOUNCE_SAMPLES = 3u
};

typedef struct
{
    uint32_t stable_levels;
    uint8_t transition_counts[USER_INPUT_COUNT];
    bool initialized;
} user_inputs_debouncer_t;

/* Levels retain electrical polarity: a set bit is high and a clear bit is
   low. The proven local/auxiliary circuits and schematic pulse-interface
   candidates are active-low. */
bool user_inputs_debouncer_init(user_inputs_debouncer_t* debouncer,
                                uint32_t raw_levels);
bool user_inputs_debouncer_update(user_inputs_debouncer_t* debouncer,
                                  uint32_t raw_levels);
uint32_t user_inputs_debounced_levels(
    const user_inputs_debouncer_t* debouncer);

#endif
