#ifndef MKS57D_CONTROL_MATH_H
#define MKS57D_CONTROL_MATH_H

#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

static inline bool finite_positive(float value)
{
    return isfinite(value) && (value > 0.0f);
}

static inline float clamp_symmetric(float value, float limit)
{
    if (value > limit)
    {
        return limit;
    }
    if (value < -limit)
    {
        return -limit;
    }
    return value;
}

static inline float q16_16_to_float(int32_t value)
{
    return (float)value / 65536.0f;
}

static inline int32_t float_to_q16_16(float value)
{
    const float maximum = 32767.9999847412109375f;

    if (value >= maximum)
    {
        return INT32_MAX;
    }
    if (value <= -32768.0f)
    {
        return INT32_MIN;
    }
    return (int32_t)(value * 65536.0f);
}

#endif
