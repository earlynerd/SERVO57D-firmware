#ifndef MKS57D_NATIVE_PROTOCOL_H
#define MKS57D_NATIVE_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mks57d/command_service.h"

enum
{
    NATIVE_PROTOCOL_VERSION_MAJOR = 1u,
    NATIVE_PROTOCOL_VERSION_MINOR = 18u,
    NATIVE_PROTOCOL_BROADCAST_ADDRESS = 0u,
    NATIVE_PROTOCOL_DEFAULT_DEVICE_ADDRESS = 1u,
    NATIVE_PROTOCOL_DEVICE_ADDRESS_MAX = 247u,
    NATIVE_PROTOCOL_MAX_PAYLOAD_SIZE = 80u,
    NATIVE_PROTOCOL_HEADER_SIZE = 8u,
    NATIVE_PROTOCOL_CRC_SIZE = 2u,
    NATIVE_PROTOCOL_MAX_DECODED_FRAME_SIZE =
        NATIVE_PROTOCOL_HEADER_SIZE + NATIVE_PROTOCOL_MAX_PAYLOAD_SIZE +
        NATIVE_PROTOCOL_CRC_SIZE,
    NATIVE_PROTOCOL_MAX_ENCODED_FRAME_SIZE =
        NATIVE_PROTOCOL_MAX_DECODED_FRAME_SIZE + 1u,
    NATIVE_PROTOCOL_MAX_WIRE_FRAME_SIZE =
        NATIVE_PROTOCOL_MAX_ENCODED_FRAME_SIZE + 1u
};

/* Decoded v1 layout, all multi-byte fields big-endian:
 * version:u8, device_address:u8, sequence:u16, message_type:u8,
 * command:u16, payload_length:u8, payload, CRC-16/CCITT-FALSE:u16.
 * The complete decoded frame is COBS encoded and terminated by 0x00.
 */

typedef enum
{
    NATIVE_PROTOCOL_MESSAGE_REQUEST = 1u,
    NATIVE_PROTOCOL_MESSAGE_RESPONSE = 2u,
    NATIVE_PROTOCOL_MESSAGE_EVENT = 3u
} native_protocol_message_type_t;

typedef enum
{
    NATIVE_PROTOCOL_COMMAND_PING = 0x0001u,
    NATIVE_PROTOCOL_COMMAND_GET_IDENTITY = 0x0002u,
    NATIVE_PROTOCOL_COMMAND_GET_CAPABILITIES = 0x0003u,
    NATIVE_PROTOCOL_COMMAND_GET_COMMISSIONING_STATUS = 0x0100u,
    NATIVE_PROTOCOL_COMMAND_CONFIGURE_CURRENT_TEST = 0x0101u,
    NATIVE_PROTOCOL_COMMAND_START_CURRENT_TEST = 0x0102u,
    NATIVE_PROTOCOL_COMMAND_STOP_CURRENT_TEST = 0x0103u,
    NATIVE_PROTOCOL_COMMAND_GET_BOOT_STATUS = 0x0104u,
    NATIVE_PROTOCOL_COMMAND_GET_ENCODER_STATUS = 0x0105u,
    NATIVE_PROTOCOL_COMMAND_GET_CURRENT_TRACE = 0x0106u,
    NATIVE_PROTOCOL_COMMAND_ARM_CURRENT_TRACE = 0x0107u,
    NATIVE_PROTOCOL_COMMAND_START_ALIGNMENT = 0x0200u,
    NATIVE_PROTOCOL_COMMAND_GET_ALIGNMENT_STATUS = 0x0201u,
    NATIVE_PROTOCOL_COMMAND_STOP_DRIVE = 0x0202u,
    NATIVE_PROTOCOL_COMMAND_CLEAR_FAULTS = 0x0203u,
    NATIVE_PROTOCOL_COMMAND_GET_CONFIGURATION_STATUS = 0x0300u,
    NATIVE_PROTOCOL_COMMAND_SAVE_CONFIGURATION = 0x0301u,
    NATIVE_PROTOCOL_COMMAND_CLEAR_CALIBRATION = 0x0302u,
    NATIVE_PROTOCOL_COMMAND_SET_CURRENT_LOOP_GAINS = 0x0303u,
    NATIVE_PROTOCOL_COMMAND_REVERT_CURRENT_LOOP_GAINS = 0x0304u,
    NATIVE_PROTOCOL_COMMAND_START_ALIGNED_TORQUE = 0x0400u,
    NATIVE_PROTOCOL_COMMAND_GET_ALIGNED_TORQUE_STATUS = 0x0401u,
    NATIVE_PROTOCOL_COMMAND_START_VELOCITY = 0x0500u,
    NATIVE_PROTOCOL_COMMAND_GET_VELOCITY_STATUS = 0x0501u,
    NATIVE_PROTOCOL_COMMAND_START_POSITION_RELATIVE = 0x0600u,
    NATIVE_PROTOCOL_COMMAND_GET_POSITION_STATUS = 0x0601u
} native_protocol_command_t;

typedef enum
{
    NATIVE_PROTOCOL_STATUS_OK = 0u,
    NATIVE_PROTOCOL_STATUS_UNKNOWN_COMMAND = 1u,
    NATIVE_PROTOCOL_STATUS_INVALID_PAYLOAD = 2u,
    NATIVE_PROTOCOL_STATUS_UNAVAILABLE = 3u,
    NATIVE_PROTOCOL_STATUS_INTERNAL_ERROR = 4u
} native_protocol_status_t;

typedef enum
{
    NATIVE_PROTOCOL_DECODE_OK = 0,
    NATIVE_PROTOCOL_DECODE_INVALID_ARGUMENT,
    NATIVE_PROTOCOL_DECODE_COBS_ERROR,
    NATIVE_PROTOCOL_DECODE_LENGTH_ERROR,
    NATIVE_PROTOCOL_DECODE_CRC_ERROR,
    NATIVE_PROTOCOL_DECODE_VERSION_ERROR
} native_protocol_decode_status_t;

typedef struct
{
    uint8_t version;
    uint8_t device_address;
    uint16_t sequence;
    uint8_t message_type;
    uint16_t command;
    size_t payload_length;
    uint8_t payload[NATIVE_PROTOCOL_MAX_PAYLOAD_SIZE];
} native_protocol_frame_t;

typedef struct
{
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
} native_protocol_stats_t;

typedef bool (*native_protocol_send_fn)(void* context,
                                        const uint8_t* bytes,
                                        size_t length);

typedef struct
{
    uint8_t device_address;
    command_service_context_t command_context;
    native_protocol_send_fn send;
    void* send_context;
    uint8_t encoded_frame[NATIVE_PROTOCOL_MAX_ENCODED_FRAME_SIZE];
    size_t encoded_length;
    bool discarding_oversize_frame;
    native_protocol_stats_t stats;
} native_protocol_server_t;

uint16_t native_protocol_crc16_ccitt_false(const uint8_t* bytes,
                                           size_t length);

size_t native_protocol_encode_wire_frame(const native_protocol_frame_t* frame,
                                         uint8_t* destination,
                                         size_t capacity);

native_protocol_decode_status_t native_protocol_decode_wire_frame(
    const uint8_t* wire_frame,
    size_t wire_length,
    native_protocol_frame_t* frame);

bool native_protocol_server_init(
    native_protocol_server_t* server,
    uint8_t device_address,
    const command_service_context_t* command_context,
    native_protocol_send_fn send,
    void* send_context);

void native_protocol_server_consume(native_protocol_server_t* server,
                                    const uint8_t* bytes,
                                    size_t length);

void native_protocol_server_get_stats(const native_protocol_server_t* server,
                                      native_protocol_stats_t* stats);

#endif
