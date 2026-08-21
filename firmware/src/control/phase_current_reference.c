#include "mks57d/phase_current_reference.h"

#include <limits.h>
#include <stddef.h>

enum
{
    SINE_TABLE_BITS = 5u,
    SINE_TABLE_SIZE = 1u << SINE_TABLE_BITS,
    SINE_INTERPOLATION_BITS = 16u,
    SINE_INDEX_SHIFT = 32u - SINE_TABLE_BITS,
    SINE_FRACTION_SHIFT = SINE_INDEX_SHIFT - SINE_INTERPOLATION_BITS,
    SINE_FRACTION_MASK = (1u << SINE_INTERPOLATION_BITS) - 1u,
    QUARTER_CYCLE_PHASE = 0x40000000u,
    SINE_Q15_SCALE = 32767u
};

static const int16_t SINE_Q15[SINE_TABLE_SIZE] = {
    0, 6393, 12539, 18204, 23170, 27245, 30273, 32137,
    32767, 32137, 30273, 27245, 23170, 18204, 12539, 6393,
    0, -6393, -12539, -18204, -23170, -27245, -30273, -32137,
    -32767, -32137, -30273, -27245, -23170, -18204, -12539, -6393,
};

static int16_t sine_q15(uint32_t phase)
{
    const uint32_t index = phase >> SINE_INDEX_SHIFT;
    const uint32_t next_index = (index + 1u) & (SINE_TABLE_SIZE - 1u);
    const uint32_t fraction =
        (phase >> SINE_FRACTION_SHIFT) & SINE_FRACTION_MASK;
    const int32_t first = SINE_Q15[index];
    const int32_t difference = (int32_t)SINE_Q15[next_index] - first;

    return (int16_t)(first +
        (int32_t)(((int64_t)difference * fraction) >>
                  SINE_INTERPOLATION_BITS));
}

static int16_t scale_reference(int16_t q15, int16_t magnitude)
{
    int32_t scaled = (int32_t)q15 * magnitude;
    int32_t rounded;

    if (scaled >= 0)
    {
        scaled += SINE_Q15_SCALE / 2u;
    }
    else
    {
        scaled -= SINE_Q15_SCALE / 2u;
    }
    rounded = scaled / SINE_Q15_SCALE;
    if (rounded > INT16_MAX)
    {
        return INT16_MAX;
    }
    if (rounded < INT16_MIN)
    {
        return INT16_MIN;
    }
    return (int16_t)rounded;
}

bool phase_current_reference_from_polar(
    int16_t magnitude_counts,
    uint32_t electrical_phase_q32,
    int16_t* current_a_reference_counts,
    int16_t* current_b_reference_counts)
{
    if ((current_a_reference_counts == NULL) ||
        (current_b_reference_counts == NULL))
    {
        return false;
    }

    *current_a_reference_counts = scale_reference(
        sine_q15(electrical_phase_q32 + QUARTER_CYCLE_PHASE),
        magnitude_counts);
    *current_b_reference_counts = scale_reference(
        sine_q15(electrical_phase_q32), magnitude_counts);
    return true;
}
