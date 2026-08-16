#include "mks57d/command_service.h"

static void response_reset(command_response_t* response)
{
    response->status = COMMAND_STATUS_INTERNAL_ERROR;
    response->kind = COMMAND_RESPONSE_NONE;
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

        default:
            response->status = COMMAND_STATUS_UNKNOWN_COMMAND;
            return;
    }
}
