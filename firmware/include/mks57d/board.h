#ifndef MKS57D_BOARD_H
#define MKS57D_BOARD_H

#include <stdbool.h>

/*
 * Safe board-level outputs only. There is deliberately no bridge-control API:
 * PA6, PA7, PB0, PB1, and the provisional PB7 nEN remain input/no-pull.
 * Low-energy peripherals may enable their GPIO ports and configure other pins.
 */
void board_init_passive(void);
bool board_passive_invariants_hold(void);
bool board_bridge_invariants_hold(void);
void board_status_led_set(bool on);
void board_status_led_toggle(void);

#endif
