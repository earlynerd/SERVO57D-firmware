#include "mks57d/diagnostics.h"

#include <stddef.h>

#include "mks57d/panic.h"
#include "mks57d/platform.h"
#include "n32l40x.h"

#if !defined(MKS57D_FIRMWARE_VERSION_MAJOR) || \
    !defined(MKS57D_FIRMWARE_VERSION_MINOR) || \
    !defined(MKS57D_FIRMWARE_VERSION_PATCH)
#error "Firmware version components must be supplied by CMake"
#endif

#define PACK_FIRMWARE_VERSION(major, minor, patch) \
    ((((uint32_t)(major) & 0xFFu) << 24) |        \
     (((uint32_t)(minor) & 0xFFu) << 16) |        \
     ((uint32_t)(patch) & 0xFFFFu))

enum
{
    DIAGNOSTICS_EXPECTED_RECORD_SIZE = 240u,
    DIAGNOSTICS_EXPECTED_SEQUENCE_OFFSET = 12u,
    DIAGNOSTICS_EXPECTED_ENCODER_OFFSET = 64u,
    DIAGNOSTICS_EXPECTED_RS485_OFFSET = 92u,
    DIAGNOSTICS_EXPECTED_PROTOCOL_OFFSET = 136u,
    DIAGNOSTICS_EXPECTED_CURRENT_LOOP_OFFSET = 184u
};

_Static_assert(sizeof(diagnostics_record_t) == DIAGNOSTICS_EXPECTED_RECORD_SIZE,
               "diagnostics ABI size changed without a schema review");
_Static_assert(offsetof(diagnostics_record_t, sequence) ==
                   DIAGNOSTICS_EXPECTED_SEQUENCE_OFFSET,
               "diagnostics ABI sequence offset changed");
_Static_assert(offsetof(diagnostics_record_t, encoder_status) ==
                   DIAGNOSTICS_EXPECTED_ENCODER_OFFSET,
               "diagnostics ABI schema-1 prefix changed");
_Static_assert(offsetof(diagnostics_record_t, rs485_status) ==
                   DIAGNOSTICS_EXPECTED_RS485_OFFSET,
               "diagnostics ABI schema-2 prefix changed");
_Static_assert(offsetof(diagnostics_record_t, native_protocol_ready) ==
                   DIAGNOSTICS_EXPECTED_PROTOCOL_OFFSET,
               "diagnostics ABI schema-3 prefix changed");
_Static_assert(offsetof(diagnostics_record_t, current_loop_ready) ==
                   DIAGNOSTICS_EXPECTED_CURRENT_LOOP_OFFSET,
               "diagnostics ABI schema-4 prefix changed");

volatile diagnostics_record_t g_diagnostics;

static uint32_t diagnostics_begin_update(void)
{
    const uint32_t odd_sequence = (g_diagnostics.sequence + 1u) | 1u;

    g_diagnostics.sequence = odd_sequence;
    __DMB();
    return odd_sequence;
}

static void diagnostics_end_update(uint32_t odd_sequence)
{
    __DMB();
    g_diagnostics.sequence = odd_sequence + 1u;
    __DMB();
}

void diagnostics_init(uint32_t app_state,
                      uint32_t uptime_millis,
                      uint32_t heartbeat_count,
                      uint32_t watchdog_status,
                      const boot_self_test_t *self_test)
{
    const panic_code_t retained_panic = g_last_panic;
    const uint32_t odd_sequence = diagnostics_begin_update();

    g_diagnostics.magic = DIAGNOSTICS_RECORD_MAGIC;
    g_diagnostics.schema_version = DIAGNOSTICS_RECORD_SCHEMA_VERSION;
    g_diagnostics.record_size = sizeof(g_diagnostics);
    g_diagnostics.firmware_version = PACK_FIRMWARE_VERSION(
        MKS57D_FIRMWARE_VERSION_MAJOR,
        MKS57D_FIRMWARE_VERSION_MINOR,
        MKS57D_FIRMWARE_VERSION_PATCH);
    g_diagnostics.capabilities = DIAGNOSTICS_CAPABILITIES_CURRENT;
    g_diagnostics.app_state = app_state;
    g_diagnostics.uptime_millis = uptime_millis;
    g_diagnostics.heartbeat_count = heartbeat_count;
    g_diagnostics.watchdog_status = watchdog_status;
    g_diagnostics.platform_boot_status =
        (uint32_t)g_platform_boot_diagnostics.status;
    g_diagnostics.reset_flags = g_platform_boot_diagnostics.reset_flags;
    g_diagnostics.retained_panic = (uint32_t)retained_panic;
    g_diagnostics.self_test_required = self_test->required;
    g_diagnostics.self_test_passed = self_test->passed;
    g_diagnostics.self_test_failed = self_test->failed;
    g_diagnostics.encoder_status = 0u;
    g_diagnostics.encoder_transport_status = 0u;
    g_diagnostics.encoder_angle_raw = 0u;
    g_diagnostics.encoder_flags = 0u;
    g_diagnostics.encoder_sample_count = 0u;
    g_diagnostics.encoder_error_count = 0u;
    g_diagnostics.encoder_last_attempt_millis = 0u;
    g_diagnostics.rs485_status = 0u;
    g_diagnostics.rs485_rx_bytes = 0u;
    g_diagnostics.rs485_rx_idle_events = 0u;
    g_diagnostics.rs485_rx_error_count = 0u;
    g_diagnostics.rs485_rx_overrun_count = 0u;
    g_diagnostics.rs485_rx_dropped_bytes = 0u;
    g_diagnostics.rs485_last_rx_byte = 0u;
    g_diagnostics.rs485_tx_bytes = 0u;
    g_diagnostics.rs485_tx_frame_count = 0u;
    g_diagnostics.rs485_tx_error_count = 0u;
    g_diagnostics.rs485_tx_busy = 0u;
    g_diagnostics.native_protocol_ready = 0u;
    g_diagnostics.native_protocol_bytes_consumed = 0u;
    g_diagnostics.native_protocol_valid_frames = 0u;
    g_diagnostics.native_protocol_responses_sent = 0u;
    g_diagnostics.native_protocol_cobs_errors = 0u;
    g_diagnostics.native_protocol_length_errors = 0u;
    g_diagnostics.native_protocol_crc_errors = 0u;
    g_diagnostics.native_protocol_version_errors = 0u;
    g_diagnostics.native_protocol_ignored_addresses = 0u;
    g_diagnostics.native_protocol_broadcasts_dropped = 0u;
    g_diagnostics.native_protocol_unexpected_message_types = 0u;
    g_diagnostics.native_protocol_transmit_rejections = 0u;
    g_diagnostics.current_loop_ready = 0u;
    g_diagnostics.current_loop_active = 0u;
    g_diagnostics.current_loop_fault_flags = 0u;
    g_diagnostics.current_loop_sample_count = 0u;
    g_diagnostics.current_loop_a_reference_counts = 0u;
    g_diagnostics.current_loop_b_reference_counts = 0u;
    g_diagnostics.current_loop_a_measured_counts = 0u;
    g_diagnostics.current_loop_b_measured_counts = 0u;
    g_diagnostics.current_loop_phase_a_voltage_permille = 0u;
    g_diagnostics.current_loop_phase_b_voltage_permille = 0u;
    g_diagnostics.current_loop_duty_a1_permille = 0u;
    g_diagnostics.current_loop_duty_a2_permille = 0u;
    g_diagnostics.current_loop_duty_b1_permille = 0u;
    g_diagnostics.current_loop_duty_b2_permille = 0u;

    diagnostics_end_update(odd_sequence);

    /* The diagnostic record now owns the preceding boot's retained history. */
    g_last_panic = PANIC_NONE;
    __DMB();
}

void diagnostics_publish(uint32_t app_state,
                         uint32_t uptime_millis,
                         uint32_t heartbeat_count,
                         uint32_t watchdog_status,
                         const boot_self_test_t *self_test)
{
    const uint32_t odd_sequence = diagnostics_begin_update();

    g_diagnostics.app_state = app_state;
    g_diagnostics.uptime_millis = uptime_millis;
    g_diagnostics.heartbeat_count = heartbeat_count;
    g_diagnostics.watchdog_status = watchdog_status;
    g_diagnostics.platform_boot_status =
        (uint32_t)g_platform_boot_diagnostics.status;
    g_diagnostics.self_test_required = self_test->required;
    g_diagnostics.self_test_passed = self_test->passed;
    g_diagnostics.self_test_failed = self_test->failed;

    diagnostics_end_update(odd_sequence);
}

void diagnostics_publish_encoder(const diagnostics_encoder_t* encoder)
{
    uint32_t odd_sequence;

    if (encoder == NULL)
    {
        return;
    }

    odd_sequence = diagnostics_begin_update();
    g_diagnostics.encoder_status = encoder->status;
    g_diagnostics.encoder_transport_status = encoder->transport_status;
    g_diagnostics.encoder_angle_raw = encoder->angle_raw;
    g_diagnostics.encoder_flags = encoder->flags;
    g_diagnostics.encoder_sample_count = encoder->sample_count;
    g_diagnostics.encoder_error_count = encoder->error_count;
    g_diagnostics.encoder_last_attempt_millis =
        encoder->last_attempt_millis;
    diagnostics_end_update(odd_sequence);
}

void diagnostics_publish_rs485(const diagnostics_rs485_t* rs485)
{
    uint32_t odd_sequence;

    if (rs485 == NULL)
    {
        return;
    }

    odd_sequence = diagnostics_begin_update();
    g_diagnostics.rs485_status = rs485->status;
    g_diagnostics.rs485_rx_bytes = rs485->rx_bytes;
    g_diagnostics.rs485_rx_idle_events = rs485->rx_idle_events;
    g_diagnostics.rs485_rx_error_count = rs485->rx_error_count;
    g_diagnostics.rs485_rx_overrun_count = rs485->rx_overrun_count;
    g_diagnostics.rs485_rx_dropped_bytes = rs485->rx_dropped_bytes;
    g_diagnostics.rs485_last_rx_byte = rs485->last_rx_byte;
    g_diagnostics.rs485_tx_bytes = rs485->tx_bytes;
    g_diagnostics.rs485_tx_frame_count = rs485->tx_frame_count;
    g_diagnostics.rs485_tx_error_count = rs485->tx_error_count;
    g_diagnostics.rs485_tx_busy = rs485->tx_busy;
    diagnostics_end_update(odd_sequence);
}

void diagnostics_publish_protocol(const diagnostics_protocol_t* protocol)
{
    uint32_t odd_sequence;

    if (protocol == NULL)
    {
        return;
    }

    odd_sequence = diagnostics_begin_update();
    g_diagnostics.native_protocol_ready = protocol->ready;
    g_diagnostics.native_protocol_bytes_consumed = protocol->bytes_consumed;
    g_diagnostics.native_protocol_valid_frames = protocol->valid_frames;
    g_diagnostics.native_protocol_responses_sent = protocol->responses_sent;
    g_diagnostics.native_protocol_cobs_errors = protocol->cobs_errors;
    g_diagnostics.native_protocol_length_errors = protocol->length_errors;
    g_diagnostics.native_protocol_crc_errors = protocol->crc_errors;
    g_diagnostics.native_protocol_version_errors = protocol->version_errors;
    g_diagnostics.native_protocol_ignored_addresses =
        protocol->ignored_addresses;
    g_diagnostics.native_protocol_broadcasts_dropped =
        protocol->broadcasts_dropped;
    g_diagnostics.native_protocol_unexpected_message_types =
        protocol->unexpected_message_types;
    g_diagnostics.native_protocol_transmit_rejections =
        protocol->transmit_rejections;
    diagnostics_end_update(odd_sequence);
}

void diagnostics_publish_current_loop(
    const diagnostics_current_loop_t* current_loop)
{
    uint32_t odd_sequence;

    if (current_loop == NULL)
    {
        return;
    }

    odd_sequence = diagnostics_begin_update();
    g_diagnostics.current_loop_ready = current_loop->ready;
    g_diagnostics.current_loop_active = current_loop->active;
    g_diagnostics.current_loop_fault_flags = current_loop->fault_flags;
    g_diagnostics.current_loop_sample_count = current_loop->sample_count;
    g_diagnostics.current_loop_a_reference_counts =
        current_loop->current_a_reference_counts;
    g_diagnostics.current_loop_b_reference_counts =
        current_loop->current_b_reference_counts;
    g_diagnostics.current_loop_a_measured_counts =
        current_loop->current_a_measured_counts;
    g_diagnostics.current_loop_b_measured_counts =
        current_loop->current_b_measured_counts;
    g_diagnostics.current_loop_phase_a_voltage_permille =
        current_loop->phase_a_voltage_permille;
    g_diagnostics.current_loop_phase_b_voltage_permille =
        current_loop->phase_b_voltage_permille;
    g_diagnostics.current_loop_duty_a1_permille =
        current_loop->duty_a1_permille;
    g_diagnostics.current_loop_duty_a2_permille =
        current_loop->duty_a2_permille;
    g_diagnostics.current_loop_duty_b1_permille =
        current_loop->duty_b1_permille;
    g_diagnostics.current_loop_duty_b2_permille =
        current_loop->duty_b2_permille;
    diagnostics_end_update(odd_sequence);
}
