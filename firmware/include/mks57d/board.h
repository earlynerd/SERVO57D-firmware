#ifndef MKS57D_BOARD_H
#define MKS57D_BOARD_H

#include <stdbool.h>

/*
 * Passive bring-up only. There is deliberately no bridge-control API in this
 * milestone: PA6, PA7, PB0, and PB1 must remain in their reset state.
 */
void board_init_passive(void);
bool board_passive_invariants_hold(void);
void board_status_led_toggle(void);

#endif
