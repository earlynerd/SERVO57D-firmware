#ifndef MKS57D_BRIDGE_CHARACTERIZER_H
#define MKS57D_BRIDGE_CHARACTERIZER_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    BRIDGE_CHARACTERIZER_LEG_A1 = 0,
    BRIDGE_CHARACTERIZER_LEG_A2,
    BRIDGE_CHARACTERIZER_LEG_B1,
    BRIDGE_CHARACTERIZER_LEG_B2,
    BRIDGE_CHARACTERIZER_LEG_COUNT
} bridge_characterizer_leg_t;

typedef struct
{
    bridge_characterizer_leg_t selected_leg;
    uint32_t previous_debounced_levels;
    bool active;
    bool enter_release_seen;
    bool initialized;
} bridge_characterizer_t;

/* Next advances the selected leg only while inactive. A debounced Enter press
 * starts the hardware PWM backend after Enter has been observed released once
 * after boot. Raw Enter release or raw Menu assertion stops immediately. */
bool bridge_characterizer_init(bridge_characterizer_t* characterizer,
                               uint32_t raw_levels,
                               uint32_t debounced_levels);
bool bridge_characterizer_update(bridge_characterizer_t* characterizer,
                                 uint32_t raw_levels,
                                 uint32_t debounced_levels);
void bridge_characterizer_stop(
    bridge_characterizer_t* characterizer);

#endif
