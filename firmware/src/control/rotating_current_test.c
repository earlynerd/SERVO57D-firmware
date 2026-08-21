#include "mks57d/rotating_current_test.h"

#include <stddef.h>

#include "mks57d/phase_current_reference.h"

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

    if (!phase_current_reference_from_polar(
            generator->amplitude_counts,
            generator->phase,
            current_a_reference_counts,
            current_b_reference_counts))
    {
        return false;
    }
    generator->phase += generator->phase_increment;
    return true;
}
