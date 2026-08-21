#ifndef MKS57D_CONFIGURATION_STORE_H
#define MKS57D_CONFIGURATION_STORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mks57d/motor_alignment.h"

enum
{
    CONFIGURATION_STORE_SLOT_COUNT = 2u,
    CONFIGURATION_STORE_PAGE_SIZE_BYTES = 2048u,
    CONFIGURATION_STORE_RECORD_SCHEMA_VERSION = 1u,
    CONFIGURATION_STORE_INVALID_SLOT = 0xFFu
};

typedef struct
{
    uint16_t encoder_counts_per_revolution;
    uint16_t electrical_cycles_per_revolution;
    motor_alignment_status_t alignment;
} product_configuration_t;

typedef bool (*configuration_store_read_word_fn)(
    void* context,
    uint8_t slot,
    size_t word_index,
    uint32_t* value);
typedef bool (*configuration_store_erase_slot_fn)(void* context,
                                                  uint8_t slot);
typedef bool (*configuration_store_program_word_fn)(
    void* context,
    uint8_t slot,
    size_t word_index,
    uint32_t value);

typedef struct
{
    void* context;
    configuration_store_read_word_fn read_word;
    configuration_store_erase_slot_fn erase_slot;
    configuration_store_program_word_fn program_word;
} configuration_store_backend_t;

typedef enum
{
    CONFIGURATION_STORE_RESULT_OK = 0,
    CONFIGURATION_STORE_RESULT_EMPTY,
    CONFIGURATION_STORE_RESULT_INVALID_ARGUMENT,
    CONFIGURATION_STORE_RESULT_IO_ERROR,
    CONFIGURATION_STORE_RESULT_VERIFY_ERROR
} configuration_store_result_t;

typedef struct
{
    configuration_store_backend_t backend;
    product_configuration_t configuration;
    uint32_t generation;
    uint8_t active_slot;
    uint8_t valid_slot_mask;
    configuration_store_result_t last_result;
    bool initialized;
    bool record_valid;
} configuration_store_t;

configuration_store_result_t configuration_store_init(
    configuration_store_t* store,
    const configuration_store_backend_t* backend);
configuration_store_result_t configuration_store_save(
    configuration_store_t* store,
    const product_configuration_t* configuration);
bool configuration_store_get(
    const configuration_store_t* store,
    product_configuration_t* configuration);
bool configuration_store_matches(
    const configuration_store_t* store,
    const product_configuration_t* configuration);
bool product_configuration_is_valid(
    const product_configuration_t* configuration);

#endif
