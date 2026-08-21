#ifndef MKS57D_BOARD_H
#define MKS57D_BOARD_H

#include <stdbool.h>
#include <stdint.h>

/* Establish and verify the reset-safe board state before peripheral and
 * current-loop bridge initialization. */
void board_init_passive(void);
bool board_passive_invariants_hold(void);
bool board_bridge_pwm_init(uint32_t timer_clock_hz);
void board_bridge_force_low_zero(void);
void board_display_reset_assert(void);
void board_display_reset_release(void);
void board_status_led_set(bool on);
void board_status_led_toggle(void);

#endif
