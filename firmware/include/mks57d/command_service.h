#ifndef MKS57D_COMMAND_SERVICE_H
#define MKS57D_COMMAND_SERVICE_H

#include <stddef.h>
#include <stdint.h>

enum
{
    COMMAND_SERVICE_PRODUCT_ID_MKS57D = 0x4D4B5335u,
    COMMAND_SERVICE_MAX_PING_BYTES = 16u
};

typedef enum
{
    COMMAND_OPERATION_PING = 0,
    COMMAND_OPERATION_GET_IDENTITY,
    COMMAND_OPERATION_GET_CAPABILITIES
} command_operation_t;

typedef enum
{
    COMMAND_STATUS_OK = 0,
    COMMAND_STATUS_UNKNOWN_COMMAND,
    COMMAND_STATUS_INVALID_PAYLOAD,
    COMMAND_STATUS_UNAVAILABLE,
    COMMAND_STATUS_INTERNAL_ERROR
} command_status_t;

typedef struct
{
    uint32_t product_id;
    uint8_t firmware_major;
    uint8_t firmware_minor;
    uint16_t firmware_patch;
    uint8_t protocol_major;
    uint8_t protocol_minor;
    uint32_t capabilities;
} command_service_context_t;

typedef struct
{
    command_operation_t operation;
    const uint8_t* payload;
    size_t payload_length;
} command_request_t;

typedef enum
{
    COMMAND_RESPONSE_NONE = 0,
    COMMAND_RESPONSE_ECHO,
    COMMAND_RESPONSE_IDENTITY,
    COMMAND_RESPONSE_CAPABILITIES
} command_response_kind_t;

typedef struct
{
    size_t length;
    uint8_t bytes[COMMAND_SERVICE_MAX_PING_BYTES];
} command_echo_response_t;

typedef struct
{
    uint32_t product_id;
    uint8_t firmware_major;
    uint8_t firmware_minor;
    uint16_t firmware_patch;
    uint8_t protocol_major;
    uint8_t protocol_minor;
} command_identity_response_t;

typedef struct
{
    command_status_t status;
    command_response_kind_t kind;
    union
    {
        command_echo_response_t echo;
        command_identity_response_t identity;
        uint32_t capabilities;
    } data;
} command_response_t;

void command_service_dispatch(const command_service_context_t* context,
                              const command_request_t* request,
                              command_response_t* response);

#endif
