#include "mks57d/command_service.h"

static void response_reset(command_response_t* response)
{
    response->status = COMMAND_STATUS_INTERNAL_ERROR;
    response->kind = COMMAND_RESPONSE_NONE;
}

static uint16_t read_u16_be(const uint8_t* bytes)
{
    return (uint16_t)(((uint16_t)bytes[0] << 8u) | bytes[1]);
}

static uint32_t read_u32_be(const uint8_t* bytes)
{
    return ((uint32_t)bytes[0] << 24u) |
           ((uint32_t)bytes[1] << 16u) |
           ((uint32_t)bytes[2] << 8u) |
           bytes[3];
}

void command_service_dispatch(const command_service_context_t* context,
                              const command_request_t* request,
                              command_response_t* response)
{
    size_t index;

    if (response == NULL)
    {
        return;
    }

    response_reset(response);
    if ((context == NULL) || (request == NULL) ||
        ((request->payload == NULL) && (request->payload_length != 0u)))
    {
        return;
    }

    switch (request->operation)
    {
        case COMMAND_OPERATION_PING:
            if (request->payload_length > COMMAND_SERVICE_MAX_PING_BYTES)
            {
                response->status = COMMAND_STATUS_INVALID_PAYLOAD;
                return;
            }

            response->status = COMMAND_STATUS_OK;
            response->kind = COMMAND_RESPONSE_ECHO;
            response->data.echo.length = request->payload_length;
            for (index = 0u; index < request->payload_length; ++index)
            {
                response->data.echo.bytes[index] = request->payload[index];
            }
            return;

        case COMMAND_OPERATION_GET_IDENTITY:
            if (request->payload_length != 0u)
            {
                response->status = COMMAND_STATUS_INVALID_PAYLOAD;
                return;
            }

            response->status = COMMAND_STATUS_OK;
            response->kind = COMMAND_RESPONSE_IDENTITY;
            response->data.identity.product_id = context->product_id;
            response->data.identity.firmware_major = context->firmware_major;
            response->data.identity.firmware_minor = context->firmware_minor;
            response->data.identity.firmware_patch = context->firmware_patch;
            response->data.identity.protocol_major = context->protocol_major;
            response->data.identity.protocol_minor = context->protocol_minor;
            return;

        case COMMAND_OPERATION_GET_CAPABILITIES:
            if (request->payload_length != 0u)
            {
                response->status = COMMAND_STATUS_INVALID_PAYLOAD;
                return;
            }

            response->status = COMMAND_STATUS_OK;
            response->kind = COMMAND_RESPONSE_CAPABILITIES;
            response->data.capabilities = context->capabilities;
            return;

        case COMMAND_OPERATION_GET_COMMISSIONING_STATUS:
            if (request->payload_length != 0u)
            {
                response->status = COMMAND_STATUS_INVALID_PAYLOAD;
                return;
            }
            if (context->commissioning.get_status == NULL)
            {
                response->status = COMMAND_STATUS_UNAVAILABLE;
                return;
            }

            response->status = context->commissioning.get_status(
                context->commissioning.context,
                &response->data.commissioning_status);
            if (response->status == COMMAND_STATUS_OK)
            {
                response->kind = COMMAND_RESPONSE_COMMISSIONING_STATUS;
            }
            return;

        case COMMAND_OPERATION_CONFIGURE_CURRENT_TEST:
            if (request->payload_length != 6u)
            {
                response->status = COMMAND_STATUS_INVALID_PAYLOAD;
                return;
            }
            if (context->commissioning.configure == NULL)
            {
                response->status = COMMAND_STATUS_UNAVAILABLE;
                return;
            }
            {
                const command_current_test_config_t requested = {
                    .amplitude_counts = read_u16_be(request->payload),
                    .frequency_millihz = read_u32_be(
                        &request->payload[2]),
                };

                response->status = context->commissioning.configure(
                    context->commissioning.context,
                    &requested,
                    &response->data.current_test_config);
                if (response->status == COMMAND_STATUS_OK)
                {
                    response->kind = COMMAND_RESPONSE_CURRENT_TEST_CONFIG;
                }
            }
            return;

        case COMMAND_OPERATION_START_CURRENT_TEST:
            if (request->payload_length != 5u)
            {
                response->status = COMMAND_STATUS_INVALID_PAYLOAD;
                return;
            }
            if (context->commissioning.start == NULL)
            {
                response->status = COMMAND_STATUS_UNAVAILABLE;
                return;
            }
            response->status = context->commissioning.start(
                context->commissioning.context,
                request->payload[0],
                read_u32_be(&request->payload[1]));
            response->kind = COMMAND_RESPONSE_NONE;
            return;

        case COMMAND_OPERATION_STOP_CURRENT_TEST:
            if (request->payload_length != 0u)
            {
                response->status = COMMAND_STATUS_INVALID_PAYLOAD;
                return;
            }
            if (context->commissioning.stop == NULL)
            {
                response->status = COMMAND_STATUS_UNAVAILABLE;
                return;
            }
            response->status = context->commissioning.stop(
                context->commissioning.context);
            response->kind = COMMAND_RESPONSE_NONE;
            return;

        case COMMAND_OPERATION_START_ALIGNMENT:
            if (request->payload_length != 2u)
            {
                response->status = COMMAND_STATUS_INVALID_PAYLOAD;
                return;
            }
            if (context->alignment.start == NULL)
            {
                response->status = COMMAND_STATUS_UNAVAILABLE;
                return;
            }
            response->status = context->alignment.start(
                context->alignment.context,
                read_u16_be(request->payload));
            response->kind = COMMAND_RESPONSE_NONE;
            return;

        case COMMAND_OPERATION_GET_ALIGNMENT_STATUS:
            if (request->payload_length != 0u)
            {
                response->status = COMMAND_STATUS_INVALID_PAYLOAD;
                return;
            }
            if (context->alignment.get_status == NULL)
            {
                response->status = COMMAND_STATUS_UNAVAILABLE;
                return;
            }
            response->status = context->alignment.get_status(
                context->alignment.context,
                &response->data.alignment_status);
            if (response->status == COMMAND_STATUS_OK)
            {
                response->kind = COMMAND_RESPONSE_ALIGNMENT_STATUS;
            }
            return;

        case COMMAND_OPERATION_STOP_DRIVE:
            if (request->payload_length != 0u)
            {
                response->status = COMMAND_STATUS_INVALID_PAYLOAD;
                return;
            }
            if (context->drive.stop == NULL)
            {
                response->status = COMMAND_STATUS_UNAVAILABLE;
                return;
            }
            response->status = context->drive.stop(
                context->drive.context);
            response->kind = COMMAND_RESPONSE_NONE;
            return;

        case COMMAND_OPERATION_CLEAR_FAULTS:
            if (request->payload_length != 0u)
            {
                response->status = COMMAND_STATUS_INVALID_PAYLOAD;
                return;
            }
            if (context->drive.clear_faults == NULL)
            {
                response->status = COMMAND_STATUS_UNAVAILABLE;
                return;
            }
            response->status = context->drive.clear_faults(
                context->drive.context,
                &response->data.fault_recovery_status);
            if (response->status == COMMAND_STATUS_OK)
            {
                response->kind = COMMAND_RESPONSE_FAULT_RECOVERY_STATUS;
            }
            return;

        case COMMAND_OPERATION_GET_CONFIGURATION_STATUS:
            if (request->payload_length != 0u)
            {
                response->status = COMMAND_STATUS_INVALID_PAYLOAD;
                return;
            }
            if (context->configuration.get_status == NULL)
            {
                response->status = COMMAND_STATUS_UNAVAILABLE;
                return;
            }
            response->status = context->configuration.get_status(
                context->configuration.context,
                &response->data.configuration_status);
            if (response->status == COMMAND_STATUS_OK)
            {
                response->kind = COMMAND_RESPONSE_CONFIGURATION_STATUS;
            }
            return;

        case COMMAND_OPERATION_SAVE_CONFIGURATION:
            if (request->payload_length != 0u)
            {
                response->status = COMMAND_STATUS_INVALID_PAYLOAD;
                return;
            }
            if (context->configuration.save == NULL)
            {
                response->status = COMMAND_STATUS_UNAVAILABLE;
                return;
            }
            response->status = context->configuration.save(
                context->configuration.context);
            response->kind = COMMAND_RESPONSE_NONE;
            return;

        case COMMAND_OPERATION_CLEAR_CALIBRATION:
            if (request->payload_length != 0u)
            {
                response->status = COMMAND_STATUS_INVALID_PAYLOAD;
                return;
            }
            if (context->configuration.clear_calibration == NULL)
            {
                response->status = COMMAND_STATUS_UNAVAILABLE;
                return;
            }
            response->status = context->configuration.clear_calibration(
                context->configuration.context);
            response->kind = COMMAND_RESPONSE_NONE;
            return;

        case COMMAND_OPERATION_START_ALIGNED_TORQUE:
            if (request->payload_length != 6u)
            {
                response->status = COMMAND_STATUS_INVALID_PAYLOAD;
                return;
            }
            if (context->aligned_torque.start == NULL)
            {
                response->status = COMMAND_STATUS_UNAVAILABLE;
                return;
            }
            response->status = context->aligned_torque.start(
                context->aligned_torque.context,
                (int16_t)read_u16_be(request->payload),
                read_u32_be(&request->payload[2]));
            response->kind = COMMAND_RESPONSE_NONE;
            return;

        case COMMAND_OPERATION_GET_ALIGNED_TORQUE_STATUS:
            if (request->payload_length != 0u)
            {
                response->status = COMMAND_STATUS_INVALID_PAYLOAD;
                return;
            }
            if (context->aligned_torque.get_status == NULL)
            {
                response->status = COMMAND_STATUS_UNAVAILABLE;
                return;
            }
            response->status = context->aligned_torque.get_status(
                context->aligned_torque.context,
                &response->data.aligned_torque_status);
            if (response->status == COMMAND_STATUS_OK)
            {
                response->kind = COMMAND_RESPONSE_ALIGNED_TORQUE_STATUS;
            }
            return;

        case COMMAND_OPERATION_START_VELOCITY:
            if (request->payload_length != 10u)
            {
                response->status = COMMAND_STATUS_INVALID_PAYLOAD;
                return;
            }
            if (context->velocity.start == NULL)
            {
                response->status = COMMAND_STATUS_UNAVAILABLE;
                return;
            }
            response->status = context->velocity.start(
                context->velocity.context,
                (int32_t)read_u32_be(request->payload),
                read_u16_be(&request->payload[4]),
                read_u32_be(&request->payload[6]));
            response->kind = COMMAND_RESPONSE_NONE;
            return;

        case COMMAND_OPERATION_GET_VELOCITY_STATUS:
            if (request->payload_length != 0u)
            {
                response->status = COMMAND_STATUS_INVALID_PAYLOAD;
                return;
            }
            if (context->velocity.get_status == NULL)
            {
                response->status = COMMAND_STATUS_UNAVAILABLE;
                return;
            }
            response->status = context->velocity.get_status(
                context->velocity.context,
                &response->data.velocity_status);
            if (response->status == COMMAND_STATUS_OK)
            {
                response->kind = COMMAND_RESPONSE_VELOCITY_STATUS;
            }
            return;

        case COMMAND_OPERATION_START_POSITION_RELATIVE:
            if (request->payload_length != 18u)
            {
                response->status = COMMAND_STATUS_INVALID_PAYLOAD;
                return;
            }
            if (context->position.start_relative == NULL)
            {
                response->status = COMMAND_STATUS_UNAVAILABLE;
                return;
            }
            response->status = context->position.start_relative(
                context->position.context,
                (int32_t)read_u32_be(request->payload),
                (int32_t)read_u32_be(&request->payload[4]),
                (int32_t)read_u32_be(&request->payload[8]),
                read_u16_be(&request->payload[12]),
                read_u32_be(&request->payload[14]));
            response->kind = COMMAND_RESPONSE_NONE;
            return;

        case COMMAND_OPERATION_GET_POSITION_STATUS:
            if (request->payload_length != 0u)
            {
                response->status = COMMAND_STATUS_INVALID_PAYLOAD;
                return;
            }
            if (context->position.get_status == NULL)
            {
                response->status = COMMAND_STATUS_UNAVAILABLE;
                return;
            }
            response->status = context->position.get_status(
                context->position.context,
                &response->data.position_status);
            if (response->status == COMMAND_STATUS_OK)
            {
                response->kind = COMMAND_RESPONSE_POSITION_STATUS;
            }
            return;

        case COMMAND_OPERATION_GET_BOOT_STATUS:
            if (request->payload_length != 0u)
            {
                response->status = COMMAND_STATUS_INVALID_PAYLOAD;
                return;
            }
            if (context->commissioning.get_boot_status == NULL)
            {
                response->status = COMMAND_STATUS_UNAVAILABLE;
                return;
            }
            response->status = context->commissioning.get_boot_status(
                context->commissioning.context,
                &response->data.boot_status);
            if (response->status == COMMAND_STATUS_OK)
            {
                response->kind = COMMAND_RESPONSE_BOOT_STATUS;
            }
            return;

        case COMMAND_OPERATION_GET_ENCODER_STATUS:
            if (request->payload_length != 0u)
            {
                response->status = COMMAND_STATUS_INVALID_PAYLOAD;
                return;
            }
            if (context->commissioning.get_encoder_status == NULL)
            {
                response->status = COMMAND_STATUS_UNAVAILABLE;
                return;
            }
            response->status = context->commissioning.get_encoder_status(
                context->commissioning.context,
                &response->data.encoder_status);
            if (response->status == COMMAND_STATUS_OK)
            {
                response->kind = COMMAND_RESPONSE_ENCODER_STATUS;
            }
            return;

        case COMMAND_OPERATION_GET_CURRENT_TRACE:
            if (request->payload_length != 2u)
            {
                response->status = COMMAND_STATUS_INVALID_PAYLOAD;
                return;
            }
            if (context->commissioning.get_current_trace == NULL)
            {
                response->status = COMMAND_STATUS_UNAVAILABLE;
                return;
            }
            response->status = context->commissioning.get_current_trace(
                context->commissioning.context,
                read_u16_be(request->payload),
                &response->data.current_trace);
            if (response->status == COMMAND_STATUS_OK)
            {
                response->kind = COMMAND_RESPONSE_CURRENT_TRACE;
            }
            return;

        default:
            response->status = COMMAND_STATUS_UNKNOWN_COMMAND;
            return;
    }
}
