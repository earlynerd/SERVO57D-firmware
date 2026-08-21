#ifndef MKS57D_MOTOR_ALIGNMENT_H
#define MKS57D_MOTOR_ALIGNMENT_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    uint16_t encoder_counts_per_revolution;
    uint16_t electrical_cycles_per_revolution;
    uint16_t maximum_quarter_step_error_counts;
} motor_alignment_config_t;

typedef struct
{
    uint16_t electrical_zero_raw;
    uint16_t observed_quarter_step_counts;
    int16_t quarter_step_error_counts;
    int8_t encoder_direction;
    bool valid;
} motor_alignment_status_t;

typedef struct
{
    motor_alignment_config_t config;
    motor_alignment_status_t status;
    bool initialized;
} motor_alignment_t;

bool motor_alignment_config_is_valid(
    const motor_alignment_config_t* config);
bool motor_alignment_init(motor_alignment_t* alignment,
                          const motor_alignment_config_t* config);
bool motor_alignment_calibrate(motor_alignment_t* alignment,
                               uint16_t phase_zero_raw,
                               uint16_t phase_quarter_raw);
bool motor_alignment_restore(motor_alignment_t* alignment,
                             const motor_alignment_status_t* status);
void motor_alignment_clear(motor_alignment_t* alignment);
bool motor_alignment_electrical_phase_q32(
    const motor_alignment_t* alignment,
    uint16_t encoder_raw,
    uint32_t* electrical_phase_q32);
void motor_alignment_get_status(const motor_alignment_t* alignment,
                                motor_alignment_status_t* status);

#endif
