#include "mks57d/rotating_current_test.h"

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

static int16_t scale_reference(int16_t q15, int16_t amplitude)
{
    int32_t scaled = (int32_t)q15 * amplitude;

    if (scaled >= 0)
    {
        scaled += SINE_Q15_SCALE / 2u;
    }
    else
    {
        scaled -= SINE_Q15_SCALE / 2u;
    }
    return (int16_t)(scaled / SINE_Q15_SCALE);
}

bool rotating_current_test_init(rotating_current_test_t* generator,
                                int16_t amplitude_counts,
                                uint32_t phase_increment,
                                uint32_t initial_phase)
{
    if ((generator == NULL) || (amplitude_counts <= 0) ||
        (phase_increment == 0u))
    {
        return false;
    }

    generator->phase = initial_phase;
    generator->phase_increment = phase_increment;
    generator->amplitude_counts = amplitude_counts;
    generator->initialized = true;
    return true;
}

bool rotating_current_test_step(rotating_current_test_t* generator,
                                int16_t* current_a_reference_counts,
                                int16_t* current_b_reference_counts)
{
    if ((generator == NULL) || !generator->initialized ||
        (current_a_reference_counts == NULL) ||
        (current_b_reference_counts == NULL))
    {
        return false;
    }

    *current_a_reference_counts = scale_reference(
        sine_q15(generator->phase + QUARTER_CYCLE_PHASE),
        generator->amplitude_counts);
    *current_b_reference_counts = scale_reference(
        sine_q15(generator->phase),
        generator->amplitude_counts);
    generator->phase += generator->phase_increment;
    return true;
}
