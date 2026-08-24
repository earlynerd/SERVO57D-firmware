#ifndef MKS57D_PHASE_CURRENT_REFERENCE_H
#define MKS57D_PHASE_CURRENT_REFERENCE_H

#include <stdbool.h>
#include <stdint.h>

bool phase_current_reference_sin_cos_q15(
    uint32_t electrical_phase_q32,
    int16_t* sine_q15,
    int16_t* cosine_q15);
bool phase_current_reference_from_polar(
    int16_t magnitude_counts,
    uint32_t electrical_phase_q32,
    int16_t* current_a_reference_counts,
    int16_t* current_b_reference_counts);

#endif
