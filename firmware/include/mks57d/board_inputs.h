#ifndef MKS57D_BOARD_INPUTS_H
#define MKS57D_BOARD_INPUTS_H

#include <stdbool.h>
#include <stdint.h>

/* Configure the five local/auxiliary inputs with pull-ups and the schematic
   PA0/PB7/PA8 pulse-interface candidates as high-impedance, no-pull inputs. */
bool board_inputs_init(void);
uint32_t board_inputs_read_raw(void);

#endif
