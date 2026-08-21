#ifndef MKS57D_DIAGNOSTICS_H
#define MKS57D_DIAGNOSTICS_H

#include <stdint.h>

#include "mks57d/boot_self_test.h"

enum
{
    DIAGNOSTICS_RECORD_MAGIC = 0x4D4B5335u,
    DIAGNOSTICS_RECORD_SCHEMA_VERSION = 5u,
    DIAGNOSTICS_CAPABILITY_PRODUCT_IMAGE = 1u << 0,
    DIAGNOSTICS_CAPABILITY_STATUS_LED = 1u << 1,
    DIAGNOSTICS_CAPABILITY_IWDG = 1u << 2,
    DIAGNOSTICS_CAPABILITY_RESET_CAUSE = 1u << 3,
    DIAGNOSTICS_CAPABILITY_PRIORITY_POLICY = 1u << 4,
    DIAGNOSTICS_CAPABILITY_ENCODER_SPI = 1u << 5,
    DIAGNOSTICS_CAPABILITY_RS485_DMA = 1u << 6,
    DIAGNOSTICS_CAPABILITY_NATIVE_PROTOCOL = 1u << 7,
    DIAGNOSTICS_CAPABILITY_DISPLAY_I2C = 1u << 8,
    DIAGNOSTICS_CAPABILITY_PASSIVE_ADC = 1u << 9,
    DIAGNOSTICS_CAPABILITY_USER_INPUTS = 1u << 10,
    DIAGNOSTICS_CAPABILITY_BRIDGE_CHARACTERIZER = 1u << 11,
    DIAGNOSTICS_CAPABILITY_CURRENT_LOOP = 1u << 12,
    DIAGNOSTICS_CAPABILITY_ALIGNMENT = 1u << 13,
    DIAGNOSTICS_CAPABILITY_PERSISTENT_CONFIGURATION = 1u << 14,
    DIAGNOSTICS_CAPABILITIES_CURRENT =
        DIAGNOSTICS_CAPABILITY_PRODUCT_IMAGE |
        DIAGNOSTICS_CAPABILITY_STATUS_LED |
        DIAGNOSTICS_CAPABILITY_IWDG |
        DIAGNOSTICS_CAPABILITY_RESET_CAUSE |
        DIAGNOSTICS_CAPABILITY_PRIORITY_POLICY |
         DIAGNOSTICS_CAPABILITY_ENCODER_SPI |
         DIAGNOSTICS_CAPABILITY_RS485_DMA |
         DIAGNOSTICS_CAPABILITY_NATIVE_PROTOCOL |
         DIAGNOSTICS_CAPABILITY_DISPLAY_I2C |
         DIAGNOSTICS_CAPABILITY_PASSIVE_ADC |
         DIAGNOSTICS_CAPABILITY_USER_INPUTS |
         DIAGNOSTICS_CAPABILITY_BRIDGE_CHARACTERIZER |
         DIAGNOSTICS_CAPABILITY_CURRENT_LOOP |
         DIAGNOSTICS_CAPABILITY_ALIGNMENT |
         DIAGNOSTICS_CAPABILITY_PERSISTENT_CONFIGURATION
};

typedef struct
{
    uint32_t status;
    uint32_t transport_status;
    uint32_t angle_raw;
    uint32_t flags;
    uint32_t sample_count;
    uint32_t error_count;
    uint32_t last_attempt_millis;
} diagnostics_encoder_t;

typedef struct
{
    uint32_t status;
    uint32_t rx_bytes;
    uint32_t rx_idle_events;
    uint32_t rx_error_count;
    uint32_t rx_overrun_count;
    uint32_t rx_dropped_bytes;
    uint32_t last_rx_byte;
    uint32_t tx_bytes;
    uint32_t tx_frame_count;
    uint32_t tx_error_count;
    uint32_t tx_busy;
} diagnostics_rs485_t;

typedef struct
{
    uint32_t ready;
    uint32_t bytes_consumed;
    uint32_t valid_frames;
    uint32_t responses_sent;
    uint32_t cobs_errors;
    uint32_t length_errors;
    uint32_t crc_errors;
    uint32_t version_errors;
    uint32_t ignored_addresses;
    uint32_t broadcasts_dropped;
    uint32_t unexpected_message_types;
    uint32_t transmit_rejections;
} diagnostics_protocol_t;

typedef struct
{
    uint32_t ready;
    uint32_t active;
    uint32_t fault_flags;
    uint32_t sample_count;
    uint32_t current_a_reference_counts;
    uint32_t current_b_reference_counts;
    uint32_t current_a_measured_counts;
    uint32_t current_b_measured_counts;
    uint32_t phase_a_voltage_permille;
    uint32_t phase_b_voltage_permille;
    uint32_t duty_a1_permille;
    uint32_t duty_a2_permille;
    uint32_t duty_b1_permille;
    uint32_t duty_b2_permille;
} diagnostics_current_loop_t;

typedef struct
{
    uint32_t magic;
    uint32_t schema_version;
    uint32_t record_size;
    uint32_t sequence;
    uint32_t firmware_version;
    uint32_t capabilities;
    uint32_t app_state;
    uint32_t uptime_millis;
    uint32_t heartbeat_count;
    uint32_t watchdog_status;
    uint32_t platform_boot_status;
    uint32_t reset_flags;
    uint32_t retained_panic;
    uint32_t self_test_required;
    uint32_t self_test_passed;
    uint32_t self_test_failed;
    uint32_t encoder_status;
    uint32_t encoder_transport_status;
    uint32_t encoder_angle_raw;
    uint32_t encoder_flags;
    uint32_t encoder_sample_count;
    uint32_t encoder_error_count;
    uint32_t encoder_last_attempt_millis;
    uint32_t rs485_status;
    uint32_t rs485_rx_bytes;
    uint32_t rs485_rx_idle_events;
    uint32_t rs485_rx_error_count;
    uint32_t rs485_rx_overrun_count;
    uint32_t rs485_rx_dropped_bytes;
    uint32_t rs485_last_rx_byte;
    uint32_t rs485_tx_bytes;
    uint32_t rs485_tx_frame_count;
    uint32_t rs485_tx_error_count;
    uint32_t rs485_tx_busy;
    uint32_t native_protocol_ready;
    uint32_t native_protocol_bytes_consumed;
    uint32_t native_protocol_valid_frames;
    uint32_t native_protocol_responses_sent;
    uint32_t native_protocol_cobs_errors;
    uint32_t native_protocol_length_errors;
    uint32_t native_protocol_crc_errors;
    uint32_t native_protocol_version_errors;
    uint32_t native_protocol_ignored_addresses;
    uint32_t native_protocol_broadcasts_dropped;
    uint32_t native_protocol_unexpected_message_types;
    uint32_t native_protocol_transmit_rejections;
    uint32_t current_loop_ready;
    uint32_t current_loop_active;
    uint32_t current_loop_fault_flags;
    uint32_t current_loop_sample_count;
    uint32_t current_loop_a_reference_counts;
    uint32_t current_loop_b_reference_counts;
    uint32_t current_loop_a_measured_counts;
    uint32_t current_loop_b_measured_counts;
    uint32_t current_loop_phase_a_voltage_permille;
    uint32_t current_loop_phase_b_voltage_permille;
    uint32_t current_loop_duty_a1_permille;
    uint32_t current_loop_duty_a2_permille;
    uint32_t current_loop_duty_b1_permille;
    uint32_t current_loop_duty_b2_permille;
} diagnostics_record_t;

extern volatile diagnostics_record_t g_diagnostics;

void diagnostics_init(uint32_t app_state,
                      uint32_t uptime_millis,
                      uint32_t heartbeat_count,
                      uint32_t watchdog_status,
                      const boot_self_test_t *self_test);
void diagnostics_publish(uint32_t app_state,
                         uint32_t uptime_millis,
                         uint32_t heartbeat_count,
                         uint32_t watchdog_status,
                         const boot_self_test_t *self_test);
void diagnostics_publish_encoder(const diagnostics_encoder_t* encoder);
void diagnostics_publish_rs485(const diagnostics_rs485_t* rs485);
void diagnostics_publish_protocol(const diagnostics_protocol_t* protocol);
void diagnostics_publish_current_loop(
    const diagnostics_current_loop_t* current_loop);

#endif
