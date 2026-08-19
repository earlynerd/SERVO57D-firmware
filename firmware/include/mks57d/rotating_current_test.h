#ifndef MKS57D_ROTATING_CURRENT_TEST_H
#define MKS57D_ROTATING_CURRENT_TEST_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    uint32_t phase;
    uint32_t phase_increment;
    int16_t amplitude_counts;
    bool initialized;
} rotating_current_test_t;

bool rotating_current_test_init(rotating_current_test_t* generator,
                                int16_t amplitude_counts,
                                uint32_t phase_increment,
                                uint32_t initial_phase);
bool rotating_current_test_step(rotating_current_test_t* generator,
                                int16_t* current_a_reference_counts,
                                int16_t* current_b_reference_counts);

#endif
