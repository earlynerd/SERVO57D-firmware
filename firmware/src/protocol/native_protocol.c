#include "mks57d/native_protocol.h"

enum
{
    FRAME_VERSION_OFFSET = 0u,
    FRAME_ADDRESS_OFFSET = 1u,
    FRAME_SEQUENCE_OFFSET = 2u,
    FRAME_MESSAGE_TYPE_OFFSET = 4u,
    FRAME_COMMAND_OFFSET = 5u,
    FRAME_PAYLOAD_LENGTH_OFFSET = 7u,
    FRAME_PAYLOAD_OFFSET = 8u
};

static uint16_t read_u16_be(const uint8_t* bytes)
{
    return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

static void write_u16_be(uint8_t* bytes, uint16_t value)
{
    bytes[0] = (uint8_t)(value >> 8);
    bytes[1] = (uint8_t)value;
}

static void write_u32_be(uint8_t* bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

uint16_t native_protocol_crc16_ccitt_false(const uint8_t* bytes,
                                           size_t length)
{
    uint16_t crc = 0xFFFFu;
    size_t index;

    if ((bytes == NULL) && (length != 0u))
    {
        return 0u;
    }

    for (index = 0u; index < length; ++index)
    {
        unsigned int bit;

        crc ^= (uint16_t)((uint16_t)bytes[index] << 8);
        for (bit = 0u; bit < 8u; ++bit)
        {
            crc = ((crc & 0x8000u) != 0u) ?
                (uint16_t)((crc << 1) ^ 0x1021u) :
                (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static size_t cobs_encode(const uint8_t* source,
                          size_t source_length,
                          uint8_t* destination,
                          size_t capacity)
{
    size_t read_index = 0u;
    size_t write_index = 1u;
    size_t code_index = 0u;
    uint8_t code = 1u;

    if ((source == NULL) || (destination == NULL) || (capacity == 0u))
    {
        return 0u;
    }

    while (read_index < source_length)
    {
        if (source[read_index] == 0u)
        {
            destination[code_index] = code;
            code = 1u;
            code_index = write_index;
            ++write_index;
            if (write_index > capacity)
            {
                return 0u;
            }
            ++read_index;
        }
        else
        {
            if (write_index >= capacity)
            {
                return 0u;
            }
            destination[write_index] = source[read_index];
            ++write_index;
            ++read_index;
            ++code;
            if (code == 0xFFu)
            {
                destination[code_index] = code;
                code = 1u;
                code_index = write_index;
                ++write_index;
                if (write_index > capacity)
                {
                    return 0u;
                }
            }
        }
    }

    destination[code_index] = code;
    return write_index;
}

static size_t cobs_decode(const uint8_t* source,
                          size_t source_length,
                          uint8_t* destination,
                          size_t capacity)
{
    size_t read_index = 0u;
    size_t write_index = 0u;

    if ((source == NULL) || (destination == NULL) || (source_length == 0u))
    {
        return 0u;
    }

    while (read_index < source_length)
    {
        const uint8_t code = source[read_index];
        size_t copy_count;
        size_t index;

        if (code == 0u)
        {
            return 0u;
        }
        ++read_index;
        copy_count = (size_t)code - 1u;
        if ((copy_count > (source_length - read_index)) ||
            (copy_count > (capacity - write_index)))
        {
            return 0u;
        }

        for (index = 0u; index < copy_count; ++index)
        {
            destination[write_index] = source[read_index];
            ++write_index;
            ++read_index;
        }

        if ((code != 0xFFu) && (read_index < source_length))
        {
            if (write_index >= capacity)
            {
                return 0u;
            }
            destination[write_index] = 0u;
            ++write_index;
        }
    }
    return write_index;
}

static native_protocol_decode_status_t decode_encoded_frame(
    const uint8_t* encoded_frame,
    size_t encoded_length,
    native_protocol_frame_t* frame)
{
    uint8_t decoded[NATIVE_PROTOCOL_MAX_DECODED_FRAME_SIZE];
    size_t decoded_length;
    size_t expected_length;
    size_t index;
    uint16_t expected_crc;
    uint16_t received_crc;

    if ((encoded_frame == NULL) || (frame == NULL))
    {
        return NATIVE_PROTOCOL_DECODE_INVALID_ARGUMENT;
    }
    if ((encoded_length == 0u) ||
        (encoded_length > NATIVE_PROTOCOL_MAX_ENCODED_FRAME_SIZE))
    {
        return NATIVE_PROTOCOL_DECODE_LENGTH_ERROR;
    }

    decoded_length = cobs_decode(encoded_frame,
                                 encoded_length,
                                 decoded,
                                 sizeof(decoded));
    if (decoded_length == 0u)
    {
        return NATIVE_PROTOCOL_DECODE_COBS_ERROR;
    }
    if (decoded_length <
        (NATIVE_PROTOCOL_HEADER_SIZE + NATIVE_PROTOCOL_CRC_SIZE))
    {
        return NATIVE_PROTOCOL_DECODE_LENGTH_ERROR;
    }

    expected_length = NATIVE_PROTOCOL_HEADER_SIZE +
                      decoded[FRAME_PAYLOAD_LENGTH_OFFSET] +
                      NATIVE_PROTOCOL_CRC_SIZE;
    if (decoded_length != expected_length)
    {
        return NATIVE_PROTOCOL_DECODE_LENGTH_ERROR;
    }

    received_crc = read_u16_be(&decoded[decoded_length - 2u]);
    expected_crc = native_protocol_crc16_ccitt_false(
        decoded,
        decoded_length - NATIVE_PROTOCOL_CRC_SIZE);
    if (received_crc != expected_crc)
    {
        return NATIVE_PROTOCOL_DECODE_CRC_ERROR;
    }
    if (decoded[FRAME_VERSION_OFFSET] != NATIVE_PROTOCOL_VERSION_MAJOR)
    {
        return NATIVE_PROTOCOL_DECODE_VERSION_ERROR;
    }

    frame->version = decoded[FRAME_VERSION_OFFSET];
    frame->device_address = decoded[FRAME_ADDRESS_OFFSET];
    frame->sequence = read_u16_be(&decoded[FRAME_SEQUENCE_OFFSET]);
    frame->message_type = decoded[FRAME_MESSAGE_TYPE_OFFSET];
    frame->command = read_u16_be(&decoded[FRAME_COMMAND_OFFSET]);
    frame->payload_length = decoded[FRAME_PAYLOAD_LENGTH_OFFSET];
    for (index = 0u; index < frame->payload_length; ++index)
    {
        frame->payload[index] = decoded[FRAME_PAYLOAD_OFFSET + index];
    }
    return NATIVE_PROTOCOL_DECODE_OK;
}

size_t native_protocol_encode_wire_frame(const native_protocol_frame_t* frame,
                                         uint8_t* destination,
                                         size_t capacity)
{
    uint8_t decoded[NATIVE_PROTOCOL_MAX_DECODED_FRAME_SIZE];
    size_t decoded_length;
    size_t encoded_length;
    size_t index;
    uint16_t crc;

    if ((frame == NULL) || (destination == NULL) ||
        (frame->payload_length > NATIVE_PROTOCOL_MAX_PAYLOAD_SIZE) ||
        (frame->payload_length > UINT8_MAX))
    {
        return 0u;
    }

    decoded[FRAME_VERSION_OFFSET] = frame->version;
    decoded[FRAME_ADDRESS_OFFSET] = frame->device_address;
    write_u16_be(&decoded[FRAME_SEQUENCE_OFFSET], frame->sequence);
    decoded[FRAME_MESSAGE_TYPE_OFFSET] = frame->message_type;
    write_u16_be(&decoded[FRAME_COMMAND_OFFSET], frame->command);
    decoded[FRAME_PAYLOAD_LENGTH_OFFSET] =
        (uint8_t)frame->payload_length;
    for (index = 0u; index < frame->payload_length; ++index)
    {
        decoded[FRAME_PAYLOAD_OFFSET + index] = frame->payload[index];
    }

    decoded_length = NATIVE_PROTOCOL_HEADER_SIZE + frame->payload_length +
                     NATIVE_PROTOCOL_CRC_SIZE;
    crc = native_protocol_crc16_ccitt_false(
        decoded,
        decoded_length - NATIVE_PROTOCOL_CRC_SIZE);
    write_u16_be(&decoded[decoded_length - NATIVE_PROTOCOL_CRC_SIZE], crc);

    if (capacity < 2u)
    {
        return 0u;
    }
    encoded_length = cobs_encode(decoded,
                                 decoded_length,
                                 destination,
                                 capacity - 1u);
    if ((encoded_length == 0u) || (encoded_length >= capacity))
    {
        return 0u;
    }
    destination[encoded_length] = 0u;
    return encoded_length + 1u;
}

native_protocol_decode_status_t native_protocol_decode_wire_frame(
    const uint8_t* wire_frame,
    size_t wire_length,
    native_protocol_frame_t* frame)
{
    if ((wire_frame == NULL) || (frame == NULL))
    {
        return NATIVE_PROTOCOL_DECODE_INVALID_ARGUMENT;
    }
    if ((wire_length < 2u) ||
        (wire_length > NATIVE_PROTOCOL_MAX_WIRE_FRAME_SIZE) ||
        (wire_frame[wire_length - 1u] != 0u))
    {
        return NATIVE_PROTOCOL_DECODE_LENGTH_ERROR;
    }
    return decode_encoded_frame(wire_frame, wire_length - 1u, frame);
}

static bool map_command(uint16_t native_command,
                        command_operation_t* operation)
{
    if (operation == NULL)
    {
        return false;
    }

    switch (native_command)
    {
        case NATIVE_PROTOCOL_COMMAND_PING:
            *operation = COMMAND_OPERATION_PING;
            return true;
        case NATIVE_PROTOCOL_COMMAND_GET_IDENTITY:
            *operation = COMMAND_OPERATION_GET_IDENTITY;
            return true;
        case NATIVE_PROTOCOL_COMMAND_GET_CAPABILITIES:
            *operation = COMMAND_OPERATION_GET_CAPABILITIES;
            return true;
        case NATIVE_PROTOCOL_COMMAND_GET_COMMISSIONING_STATUS:
            *operation = COMMAND_OPERATION_GET_COMMISSIONING_STATUS;
            return true;
        case NATIVE_PROTOCOL_COMMAND_CONFIGURE_CURRENT_TEST:
            *operation = COMMAND_OPERATION_CONFIGURE_CURRENT_TEST;
            return true;
        case NATIVE_PROTOCOL_COMMAND_START_CURRENT_TEST:
            *operation = COMMAND_OPERATION_START_CURRENT_TEST;
            return true;
        case NATIVE_PROTOCOL_COMMAND_STOP_CURRENT_TEST:
            *operation = COMMAND_OPERATION_STOP_CURRENT_TEST;
            return true;
        case NATIVE_PROTOCOL_COMMAND_GET_BOOT_STATUS:
            *operation = COMMAND_OPERATION_GET_BOOT_STATUS;
            return true;
        case NATIVE_PROTOCOL_COMMAND_GET_ENCODER_STATUS:
            *operation = COMMAND_OPERATION_GET_ENCODER_STATUS;
            return true;
        case NATIVE_PROTOCOL_COMMAND_GET_CURRENT_TRACE:
            *operation = COMMAND_OPERATION_GET_CURRENT_TRACE;
            return true;
        case NATIVE_PROTOCOL_COMMAND_START_ALIGNMENT:
            *operation = COMMAND_OPERATION_START_ALIGNMENT;
            return true;
        case NATIVE_PROTOCOL_COMMAND_GET_ALIGNMENT_STATUS:
            *operation = COMMAND_OPERATION_GET_ALIGNMENT_STATUS;
            return true;
        case NATIVE_PROTOCOL_COMMAND_STOP_DRIVE:
            *operation = COMMAND_OPERATION_STOP_DRIVE;
            return true;
        case NATIVE_PROTOCOL_COMMAND_GET_CONFIGURATION_STATUS:
            *operation = COMMAND_OPERATION_GET_CONFIGURATION_STATUS;
            return true;
        case NATIVE_PROTOCOL_COMMAND_SAVE_CONFIGURATION:
            *operation = COMMAND_OPERATION_SAVE_CONFIGURATION;
            return true;
        case NATIVE_PROTOCOL_COMMAND_CLEAR_CALIBRATION:
            *operation = COMMAND_OPERATION_CLEAR_CALIBRATION;
            return true;
        default:
            return false;
    }
}

static uint8_t map_status(command_status_t status)
{
    switch (status)
    {
        case COMMAND_STATUS_OK:
            return NATIVE_PROTOCOL_STATUS_OK;
        case COMMAND_STATUS_UNKNOWN_COMMAND:
            return NATIVE_PROTOCOL_STATUS_UNKNOWN_COMMAND;
        case COMMAND_STATUS_INVALID_PAYLOAD:
            return NATIVE_PROTOCOL_STATUS_INVALID_PAYLOAD;
        case COMMAND_STATUS_UNAVAILABLE:
            return NATIVE_PROTOCOL_STATUS_UNAVAILABLE;
        default:
            return NATIVE_PROTOCOL_STATUS_INTERNAL_ERROR;
    }
}

static bool serialize_response(const native_protocol_frame_t* request_frame,
                               const command_response_t* command_response,
                               native_protocol_frame_t* response_frame)
{
    size_t index;
    size_t payload_length = 1u;

    response_frame->version = NATIVE_PROTOCOL_VERSION_MAJOR;
    response_frame->device_address = request_frame->device_address;
    response_frame->sequence = request_frame->sequence;
    response_frame->message_type = NATIVE_PROTOCOL_MESSAGE_RESPONSE;
    response_frame->command = request_frame->command;
    response_frame->payload[0] = map_status(command_response->status);

    if (command_response->status == COMMAND_STATUS_OK)
    {
        switch (command_response->kind)
        {
            case COMMAND_RESPONSE_ECHO:
                if ((command_response->data.echo.length + 1u) >
                    NATIVE_PROTOCOL_MAX_PAYLOAD_SIZE)
                {
                    return false;
                }
                for (index = 0u;
                     index < command_response->data.echo.length;
                     ++index)
                {
                    response_frame->payload[index + 1u] =
                        command_response->data.echo.bytes[index];
                }
                payload_length += command_response->data.echo.length;
                break;

            case COMMAND_RESPONSE_IDENTITY:
                write_u32_be(&response_frame->payload[1],
                             command_response->data.identity.product_id);
                response_frame->payload[5] =
                    command_response->data.identity.firmware_major;
                response_frame->payload[6] =
                    command_response->data.identity.firmware_minor;
                write_u16_be(&response_frame->payload[7],
                             command_response->data.identity.firmware_patch);
                response_frame->payload[9] =
                    command_response->data.identity.protocol_major;
                response_frame->payload[10] =
                    command_response->data.identity.protocol_minor;
                payload_length = 11u;
                break;

            case COMMAND_RESPONSE_CAPABILITIES:
                write_u32_be(&response_frame->payload[1],
                             command_response->data.capabilities);
                payload_length = 5u;
                break;

            case COMMAND_RESPONSE_COMMISSIONING_STATUS:
            {
                const command_commissioning_status_t* status =
                    &command_response->data.commissioning_status;

                response_frame->payload[1] = status->schema_version;
                write_u32_be(&response_frame->payload[2], status->flags);
                response_frame->payload[6] = status->raw_input_levels;
                response_frame->payload[7] =
                    status->debounced_input_levels;
                response_frame->payload[8] = status->adc_status;
                response_frame->payload[9] = status->selected_leg;
                write_u32_be(&response_frame->payload[10],
                             status->fault_flags);
                write_u32_be(&response_frame->payload[14],
                             status->sample_count);
                write_u16_be(&response_frame->payload[18],
                             status->current_a_raw);
                write_u16_be(&response_frame->payload[20],
                             status->current_b_raw);
                write_u16_be(&response_frame->payload[22],
                             status->current_a_zero_raw);
                write_u16_be(&response_frame->payload[24],
                             status->current_b_zero_raw);
                write_u16_be(&response_frame->payload[26],
                             (uint16_t)status->current_a_reference_counts);
                write_u16_be(&response_frame->payload[28],
                             (uint16_t)status->current_b_reference_counts);
                write_u16_be(&response_frame->payload[30],
                             (uint16_t)status->current_a_measured_counts);
                write_u16_be(&response_frame->payload[32],
                             (uint16_t)status->current_b_measured_counts);
                write_u16_be(&response_frame->payload[34],
                             (uint16_t)status->phase_a_voltage_permille);
                write_u16_be(&response_frame->payload[36],
                             (uint16_t)status->phase_b_voltage_permille);
                write_u16_be(&response_frame->payload[38],
                             status->duty_a1_permille);
                write_u16_be(&response_frame->payload[40],
                             status->duty_a2_permille);
                write_u16_be(&response_frame->payload[42],
                             status->duty_b1_permille);
                write_u16_be(&response_frame->payload[44],
                             status->duty_b2_permille);
                write_u16_be(&response_frame->payload[46],
                             status->test_amplitude_counts);
                write_u16_be(&response_frame->payload[48],
                             status->maximum_test_amplitude_counts);
                write_u16_be(&response_frame->payload[50],
                             status->hard_current_limit_counts);
                write_u16_be(&response_frame->payload[52],
                             status->phase_voltage_limit_permille);
                write_u32_be(&response_frame->payload[54],
                             status->test_frequency_millihz);
                write_u32_be(&response_frame->payload[58],
                             status->remote_run_remaining_millis);
                response_frame->payload[62] = status->retained_panic;
                response_frame->payload[63] = status->watchdog_reset;
                payload_length = 64u;
                break;
            }

            case COMMAND_RESPONSE_CURRENT_TEST_CONFIG:
                write_u16_be(
                    &response_frame->payload[1],
                    command_response->data.current_test_config.
                        amplitude_counts);
                write_u32_be(
                    &response_frame->payload[3],
                    command_response->data.current_test_config.
                        frequency_millihz);
                payload_length = 7u;
                break;

            case COMMAND_RESPONSE_BOOT_STATUS:
                response_frame->payload[1] =
                    command_response->data.boot_status.schema_version;
                write_u32_be(
                    &response_frame->payload[2],
                    command_response->data.boot_status.reset_flags);
                response_frame->payload[6] =
                    command_response->data.boot_status.retained_panic;
                write_u32_be(
                    &response_frame->payload[7],
                    command_response->data.boot_status.uptime_millis);
                payload_length = 11u;
                break;

            case COMMAND_RESPONSE_ENCODER_STATUS:
                response_frame->payload[1] =
                    command_response->data.encoder_status.schema_version;
                response_frame->payload[2] =
                    command_response->data.encoder_status.status;
                response_frame->payload[3] =
                    command_response->data.encoder_status.transport_status;
                write_u16_be(
                    &response_frame->payload[4],
                    command_response->data.encoder_status.angle_raw);
                response_frame->payload[6] =
                    command_response->data.encoder_status.flags;
                write_u32_be(
                    &response_frame->payload[7],
                    command_response->data.encoder_status.sample_count);
                write_u32_be(
                    &response_frame->payload[11],
                    command_response->data.encoder_status.error_count);
                write_u32_be(
                    &response_frame->payload[15],
                    command_response->data.encoder_status.
                        last_attempt_millis);
                response_frame->payload[19] =
                    command_response->data.encoder_status.estimator_flags;
                write_u32_be(
                    &response_frame->payload[20],
                    (uint32_t)command_response->data.encoder_status.
                        position_revolutions_q16_16);
                write_u32_be(
                    &response_frame->payload[24],
                    (uint32_t)command_response->data.encoder_status.
                        velocity_revolutions_per_second_q16_16);
                write_u32_be(
                    &response_frame->payload[28],
                    command_response->data.encoder_status.
                        estimator_timestamp_us);
                write_u32_be(
                    &response_frame->payload[32],
                    command_response->data.encoder_status.
                        estimator_fault_flags);
                write_u16_be(
                    &response_frame->payload[36],
                    command_response->data.encoder_status.
                        alignment_zero_raw);
                response_frame->payload[38] = (uint8_t)
                    command_response->data.encoder_status.
                        alignment_direction;
                write_u32_be(
                    &response_frame->payload[39],
                    command_response->data.encoder_status.
                        electrical_phase_q32);
                write_u32_be(
                    &response_frame->payload[43],
                    command_response->data.encoder_status.
                        estimator_sample_interval_us);
                write_u32_be(
                    &response_frame->payload[47],
                    command_response->data.encoder_status.
                        estimator_maximum_sample_interval_us);
                payload_length = 51u;
                break;

            case COMMAND_RESPONSE_CURRENT_TRACE:
            {
                const command_current_trace_sample_t* trace =
                    &command_response->data.current_trace;

                response_frame->payload[1] = trace->schema_version;
                write_u16_be(&response_frame->payload[2],
                             trace->captured_sample_count);
                write_u16_be(&response_frame->payload[4],
                             trace->sample_index);
                write_u32_be(&response_frame->payload[6],
                             trace->loop_sample_count);
                write_u16_be(&response_frame->payload[10],
                             (uint16_t)trace->current_a_reference_counts);
                write_u16_be(&response_frame->payload[12],
                             (uint16_t)trace->current_b_reference_counts);
                write_u16_be(&response_frame->payload[14],
                             (uint16_t)trace->current_a_measured_counts);
                write_u16_be(&response_frame->payload[16],
                             (uint16_t)trace->current_b_measured_counts);
                write_u16_be(&response_frame->payload[18],
                             (uint16_t)trace->phase_a_voltage_permille);
                write_u16_be(&response_frame->payload[20],
                             (uint16_t)trace->phase_b_voltage_permille);
                payload_length = 22u;
                break;
            }

            case COMMAND_RESPONSE_ALIGNMENT_STATUS:
            {
                const command_alignment_status_t* status =
                    &command_response->data.alignment_status;

                response_frame->payload[1] = status->schema_version;
                response_frame->payload[2] = status->state;
                response_frame->payload[3] = status->result;
                response_frame->payload[4] = status->flags;
                write_u16_be(&response_frame->payload[5],
                             status->alignment_current_counts);
                write_u16_be(&response_frame->payload[7],
                             status->phase_zero_raw);
                write_u16_be(&response_frame->payload[9],
                             status->phase_quarter_raw);
                write_u16_be(&response_frame->payload[11],
                             status->return_zero_raw);
                write_u16_be(&response_frame->payload[13],
                             status->observed_quarter_step_counts);
                write_u16_be(&response_frame->payload[15],
                             (uint16_t)status->quarter_step_error_counts);
                write_u16_be(&response_frame->payload[17],
                             (uint16_t)status->closure_error_counts);
                response_frame->payload[19] =
                    (uint8_t)status->encoder_direction;
                write_u16_be(&response_frame->payload[20],
                             status->active_sample_count);
                write_u32_be(&response_frame->payload[22],
                             status->elapsed_millis);
                write_u32_be(&response_frame->payload[26],
                             status->remaining_millis);
                write_u16_be(&response_frame->payload[30],
                             status->minimum_current_counts);
                write_u16_be(&response_frame->payload[32],
                             status->maximum_current_counts);
                write_u16_be(&response_frame->payload[34],
                             status->expected_quarter_step_counts);
                write_u16_be(&response_frame->payload[36],
                             status->maximum_quarter_step_error_counts);
                write_u32_be(&response_frame->payload[38],
                             status->settle_duration_millis);
                write_u32_be(&response_frame->payload[42],
                             status->sample_duration_millis);
                write_u32_be(&response_frame->payload[46],
                             status->maximum_duration_millis);
                write_u16_be(&response_frame->payload[50],
                             status->minimum_sample_count);
                write_u16_be(&response_frame->payload[52],
                             status->maximum_sample_span_counts);
                write_u16_be(&response_frame->payload[54],
                             status->maximum_closure_error_counts);
                write_u16_be(&response_frame->payload[56],
                             status->maximum_current_error_counts);
                payload_length = 58u;
                break;
            }

            case COMMAND_RESPONSE_CONFIGURATION_STATUS:
            {
                const command_configuration_status_t* status =
                    &command_response->data.configuration_status;

                response_frame->payload[1] = status->schema_version;
                response_frame->payload[2] = status->flags;
                response_frame->payload[3] = status->last_result;
                response_frame->payload[4] = status->active_slot;
                write_u16_be(&response_frame->payload[5],
                             status->record_schema_version);
                write_u32_be(&response_frame->payload[7],
                             status->generation);
                write_u16_be(&response_frame->payload[11],
                             status->stored_encoder_counts_per_revolution);
                write_u16_be(&response_frame->payload[13],
                             status->stored_electrical_cycles_per_revolution);
                write_u16_be(&response_frame->payload[15],
                             status->stored_electrical_zero_raw);
                write_u16_be(&response_frame->payload[17],
                             status->stored_observed_quarter_step_counts);
                write_u16_be(&response_frame->payload[19],
                             (uint16_t)status->
                                 stored_quarter_step_error_counts);
                response_frame->payload[21] =
                    (uint8_t)status->stored_encoder_direction;
                write_u16_be(&response_frame->payload[22],
                             status->active_encoder_counts_per_revolution);
                write_u16_be(&response_frame->payload[24],
                             status->active_electrical_cycles_per_revolution);
                write_u16_be(&response_frame->payload[26],
                             status->active_electrical_zero_raw);
                write_u16_be(&response_frame->payload[28],
                             status->active_observed_quarter_step_counts);
                write_u16_be(&response_frame->payload[30],
                             (uint16_t)status->
                                 active_quarter_step_error_counts);
                response_frame->payload[32] =
                    (uint8_t)status->active_encoder_direction;
                payload_length = 33u;
                break;
            }

            case COMMAND_RESPONSE_NONE:
                payload_length = 1u;
                break;

            default:
                return false;
        }
    }

    response_frame->payload_length = payload_length;
    return true;
}

static void respond_to_request(native_protocol_server_t* server,
                               const native_protocol_frame_t* frame)
{
    command_request_t command_request;
    command_response_t command_response;
    native_protocol_frame_t response_frame;
    uint8_t wire_response[NATIVE_PROTOCOL_MAX_WIRE_FRAME_SIZE];
    size_t wire_length;

    if (!map_command(frame->command, &command_request.operation))
    {
        command_response.status = COMMAND_STATUS_UNKNOWN_COMMAND;
        command_response.kind = COMMAND_RESPONSE_NONE;
    }
    else
    {
        command_request.payload = frame->payload;
        command_request.payload_length = frame->payload_length;
        command_service_dispatch(&server->command_context,
                                 &command_request,
                                 &command_response);
    }

    if (!serialize_response(frame, &command_response, &response_frame))
    {
        command_response.status = COMMAND_STATUS_INTERNAL_ERROR;
        command_response.kind = COMMAND_RESPONSE_NONE;
        if (!serialize_response(frame, &command_response, &response_frame))
        {
            ++server->stats.transmit_rejections;
            return;
        }
    }

    wire_length = native_protocol_encode_wire_frame(
        &response_frame,
        wire_response,
        sizeof(wire_response));
    if ((wire_length == 0u) ||
        !server->send(server->send_context, wire_response, wire_length))
    {
        ++server->stats.transmit_rejections;
        return;
    }
    ++server->stats.responses_sent;
}

static void handle_decoded_frame(native_protocol_server_t* server,
                                 const native_protocol_frame_t* frame)
{
    ++server->stats.valid_frames;
    if ((frame->device_address != server->device_address) &&
        (frame->device_address != NATIVE_PROTOCOL_BROADCAST_ADDRESS))
    {
        ++server->stats.ignored_addresses;
        return;
    }
    if (frame->message_type != NATIVE_PROTOCOL_MESSAGE_REQUEST)
    {
        ++server->stats.unexpected_message_types;
        return;
    }
    if (frame->device_address == NATIVE_PROTOCOL_BROADCAST_ADDRESS)
    {
        ++server->stats.broadcasts_dropped;
        return;
    }

    respond_to_request(server, frame);
}

static void process_encoded_frame(native_protocol_server_t* server)
{
    native_protocol_frame_t frame;
    const native_protocol_decode_status_t status = decode_encoded_frame(
        server->encoded_frame,
        server->encoded_length,
        &frame);

    switch (status)
    {
        case NATIVE_PROTOCOL_DECODE_OK:
            handle_decoded_frame(server, &frame);
            break;
        case NATIVE_PROTOCOL_DECODE_COBS_ERROR:
            ++server->stats.cobs_errors;
            break;
        case NATIVE_PROTOCOL_DECODE_CRC_ERROR:
            ++server->stats.crc_errors;
            break;
        case NATIVE_PROTOCOL_DECODE_VERSION_ERROR:
            ++server->stats.version_errors;
            break;
        default:
            ++server->stats.length_errors;
            break;
    }
}

bool native_protocol_server_init(
    native_protocol_server_t* server,
    uint8_t device_address,
    const command_service_context_t* command_context,
    native_protocol_send_fn send,
    void* send_context)
{
    native_protocol_stats_t empty_stats = {0};

    if ((server == NULL) || (command_context == NULL) || (send == NULL) ||
        (device_address == NATIVE_PROTOCOL_BROADCAST_ADDRESS) ||
        (device_address > NATIVE_PROTOCOL_DEVICE_ADDRESS_MAX))
    {
        return false;
    }

    server->device_address = device_address;
    server->command_context = *command_context;
    server->send = send;
    server->send_context = send_context;
    server->encoded_length = 0u;
    server->discarding_oversize_frame = false;
    server->stats = empty_stats;
    return true;
}

void native_protocol_server_consume(native_protocol_server_t* server,
                                    const uint8_t* bytes,
                                    size_t length)
{
    size_t index;

    if ((server == NULL) || ((bytes == NULL) && (length != 0u)))
    {
        return;
    }

    for (index = 0u; index < length; ++index)
    {
        const uint8_t byte = bytes[index];

        ++server->stats.bytes_consumed;
        if (byte == 0u)
        {
            if (server->discarding_oversize_frame)
            {
                server->discarding_oversize_frame = false;
                server->encoded_length = 0u;
                continue;
            }
            if (server->encoded_length != 0u)
            {
                process_encoded_frame(server);
                server->encoded_length = 0u;
            }
            continue;
        }

        if (server->discarding_oversize_frame)
        {
            continue;
        }
        if (server->encoded_length >=
            NATIVE_PROTOCOL_MAX_ENCODED_FRAME_SIZE)
        {
            server->discarding_oversize_frame = true;
            server->encoded_length = 0u;
            ++server->stats.length_errors;
            continue;
        }
        server->encoded_frame[server->encoded_length] = byte;
        ++server->encoded_length;
    }
}

void native_protocol_server_get_stats(const native_protocol_server_t* server,
                                      native_protocol_stats_t* stats)
{
    if ((server == NULL) || (stats == NULL))
    {
        return;
    }
    *stats = server->stats;
}
