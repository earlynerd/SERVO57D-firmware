#include "mks57d/configuration_store.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

enum
{
    CONFIGURATION_RECORD_MAGIC = 0x4D4B4346u,
    CONFIGURATION_RECORD_COMMIT = 0x434D4954u,
    CONFIGURATION_RECORD_WORD_COUNT = 8u,
    CONFIGURATION_RECORD_CRC_WORD_INDEX = 6u,
    CONFIGURATION_RECORD_COMMIT_WORD_INDEX = 7u,
    CONFIGURATION_RECORD_ALIGNMENT_VALID = 1u << 24
};

static const uint32_t CONFIGURATION_RECORD_RESERVED_MASK = 0xFE000000u;

_Static_assert(
    CONFIGURATION_RECORD_WORD_COUNT * sizeof(uint32_t) <=
        CONFIGURATION_STORE_PAGE_SIZE_BYTES,
    "configuration record must fit one erase page");

typedef struct
{
    product_configuration_t configuration;
    uint32_t generation;
    uint32_t words[CONFIGURATION_RECORD_WORD_COUNT];
} decoded_record_t;

static uint32_t crc32_update(uint32_t crc, uint8_t byte)
{
    unsigned int bit;

    crc ^= byte;
    for (bit = 0u; bit < 8u; ++bit)
    {
        crc = (crc & 1u) != 0u ?
            (crc >> 1u) ^ 0xEDB88320u : crc >> 1u;
    }
    return crc;
}

static uint32_t record_crc32(const uint32_t* words, size_t word_count)
{
    uint32_t crc = UINT32_MAX;
    size_t word_index;

    for (word_index = 0u; word_index < word_count; ++word_index)
    {
        unsigned int byte_index;

        for (byte_index = 0u; byte_index < 4u; ++byte_index)
        {
            crc = crc32_update(
                crc,
                (uint8_t)(words[word_index] >> (byte_index * 8u)));
        }
    }
    return crc ^ UINT32_MAX;
}

static bool configurations_equal(const product_configuration_t* left,
                                 const product_configuration_t* right)
{
    return (left->encoder_counts_per_revolution ==
            right->encoder_counts_per_revolution) &&
           (left->electrical_cycles_per_revolution ==
            right->electrical_cycles_per_revolution) &&
           (left->alignment.electrical_zero_raw ==
            right->alignment.electrical_zero_raw) &&
           (left->alignment.observed_quarter_step_counts ==
            right->alignment.observed_quarter_step_counts) &&
           (left->alignment.quarter_step_error_counts ==
            right->alignment.quarter_step_error_counts) &&
           (left->alignment.encoder_direction ==
            right->alignment.encoder_direction) &&
           (left->alignment.valid == right->alignment.valid);
}

bool product_configuration_is_valid(
    const product_configuration_t* configuration)
{
    uint32_t denominator;
    uint32_t expected_quarter_step;
    int32_t expected_error;

    if ((configuration == NULL) ||
        (configuration->encoder_counts_per_revolution < 8u) ||
        (configuration->electrical_cycles_per_revolution == 0u))
    {
        return false;
    }
    denominator =
        (uint32_t)configuration->electrical_cycles_per_revolution * 4u;
    if (denominator >
        ((uint32_t)configuration->encoder_counts_per_revolution / 2u))
    {
        return false;
    }

    if (!configuration->alignment.valid)
    {
        return (configuration->alignment.electrical_zero_raw == 0u) &&
               (configuration->alignment.observed_quarter_step_counts == 0u) &&
               (configuration->alignment.quarter_step_error_counts == 0) &&
               (configuration->alignment.encoder_direction == 0);
    }
    if (((uint32_t)configuration->alignment.electrical_zero_raw >=
         configuration->encoder_counts_per_revolution) ||
        (configuration->alignment.observed_quarter_step_counts == 0u) ||
        ((uint32_t)configuration->alignment.observed_quarter_step_counts >=
         ((uint32_t)configuration->encoder_counts_per_revolution / 2u)) ||
        ((configuration->alignment.encoder_direction != 1) &&
         (configuration->alignment.encoder_direction != -1)))
    {
        return false;
    }

    expected_quarter_step =
        ((uint32_t)configuration->encoder_counts_per_revolution +
         (denominator / 2u)) /
        denominator;
    expected_error =
        (int32_t)configuration->alignment.observed_quarter_step_counts -
        (int32_t)expected_quarter_step;
    return (expected_error >= INT16_MIN) &&
           (expected_error <= INT16_MAX) &&
           ((int16_t)expected_error ==
            configuration->alignment.quarter_step_error_counts);
}

static void encode_record(const product_configuration_t* configuration,
                          uint32_t generation,
                          uint32_t* words)
{
    memset(words, 0, CONFIGURATION_RECORD_WORD_COUNT * sizeof(words[0]));
    words[0] = CONFIGURATION_RECORD_MAGIC;
    words[1] =
        ((uint32_t)CONFIGURATION_STORE_RECORD_SCHEMA_VERSION << 16u) |
        CONFIGURATION_RECORD_WORD_COUNT;
    words[2] = generation;
    words[3] =
        (uint32_t)configuration->encoder_counts_per_revolution |
        ((uint32_t)configuration->electrical_cycles_per_revolution << 16u);
    words[4] =
        (uint32_t)configuration->alignment.electrical_zero_raw |
        ((uint32_t)configuration->alignment.observed_quarter_step_counts <<
         16u);
    words[5] =
        (uint32_t)(uint16_t)configuration->alignment.
            quarter_step_error_counts |
        ((uint32_t)(uint8_t)configuration->alignment.encoder_direction <<
         16u) |
        (configuration->alignment.valid ?
             CONFIGURATION_RECORD_ALIGNMENT_VALID : 0u);
    words[CONFIGURATION_RECORD_CRC_WORD_INDEX] = record_crc32(
        words, CONFIGURATION_RECORD_CRC_WORD_INDEX);
    words[CONFIGURATION_RECORD_COMMIT_WORD_INDEX] =
        CONFIGURATION_RECORD_COMMIT;
}

static bool read_record(const configuration_store_backend_t* backend,
                        uint8_t slot,
                        decoded_record_t* record,
                        bool* io_error)
{
    size_t index;
    uint32_t header;

    for (index = 0u; index < CONFIGURATION_RECORD_WORD_COUNT; ++index)
    {
        if (!backend->read_word(
                backend->context, slot, index, &record->words[index]))
        {
            *io_error = true;
            return false;
        }
    }
    if ((record->words[0] != CONFIGURATION_RECORD_MAGIC) ||
        (record->words[CONFIGURATION_RECORD_COMMIT_WORD_INDEX] !=
         CONFIGURATION_RECORD_COMMIT))
    {
        return false;
    }
    header = record->words[1];
    if (((header >> 16u) !=
         CONFIGURATION_STORE_RECORD_SCHEMA_VERSION) ||
        ((header & 0xFFFFu) != CONFIGURATION_RECORD_WORD_COUNT) ||
        (record->words[CONFIGURATION_RECORD_CRC_WORD_INDEX] !=
         record_crc32(record->words,
                      CONFIGURATION_RECORD_CRC_WORD_INDEX)) ||
        ((record->words[5] & CONFIGURATION_RECORD_RESERVED_MASK) != 0u))
    {
        return false;
    }

    memset(&record->configuration, 0, sizeof(record->configuration));
    record->generation = record->words[2];
    record->configuration.encoder_counts_per_revolution =
        (uint16_t)record->words[3];
    record->configuration.electrical_cycles_per_revolution =
        (uint16_t)(record->words[3] >> 16u);
    record->configuration.alignment.electrical_zero_raw =
        (uint16_t)record->words[4];
    record->configuration.alignment.observed_quarter_step_counts =
        (uint16_t)(record->words[4] >> 16u);
    record->configuration.alignment.quarter_step_error_counts =
        (int16_t)record->words[5];
    record->configuration.alignment.encoder_direction =
        (int8_t)(record->words[5] >> 16u);
    record->configuration.alignment.valid =
        (record->words[5] & CONFIGURATION_RECORD_ALIGNMENT_VALID) != 0u;
    return product_configuration_is_valid(&record->configuration);
}

static bool generation_is_newer(uint32_t candidate, uint32_t reference)
{
    const uint32_t distance = candidate - reference;

    return (distance != 0u) && (distance < 0x80000000u);
}

configuration_store_result_t configuration_store_init(
    configuration_store_t* store,
    const configuration_store_backend_t* backend)
{
    decoded_record_t records[CONFIGURATION_STORE_SLOT_COUNT];
    bool valid[CONFIGURATION_STORE_SLOT_COUNT] = {false, false};
    bool io_error = false;
    uint8_t selected = CONFIGURATION_STORE_INVALID_SLOT;
    uint8_t slot;

    if ((store == NULL) || (backend == NULL) ||
        (backend->read_word == NULL) || (backend->erase_slot == NULL) ||
        (backend->program_word == NULL))
    {
        return CONFIGURATION_STORE_RESULT_INVALID_ARGUMENT;
    }

    memset(store, 0, sizeof(*store));
    store->backend = *backend;
    store->active_slot = CONFIGURATION_STORE_INVALID_SLOT;
    for (slot = 0u; slot < CONFIGURATION_STORE_SLOT_COUNT; ++slot)
    {
        valid[slot] = read_record(
            &store->backend, slot, &records[slot], &io_error);
        if (valid[slot])
        {
            store->valid_slot_mask |= (uint8_t)(1u << slot);
        }
    }
    if (valid[0])
    {
        selected = 0u;
    }
    if (valid[1] &&
        ((selected == CONFIGURATION_STORE_INVALID_SLOT) ||
         generation_is_newer(records[1].generation,
                             records[selected].generation)))
    {
        selected = 1u;
    }

    store->initialized = true;
    if (selected != CONFIGURATION_STORE_INVALID_SLOT)
    {
        store->configuration = records[selected].configuration;
        store->generation = records[selected].generation;
        store->active_slot = selected;
        store->record_valid = true;
        store->last_result = CONFIGURATION_STORE_RESULT_OK;
    }
    else
    {
        store->last_result = io_error ?
            CONFIGURATION_STORE_RESULT_IO_ERROR :
            CONFIGURATION_STORE_RESULT_EMPTY;
    }
    return store->last_result;
}

configuration_store_result_t configuration_store_save(
    configuration_store_t* store,
    const product_configuration_t* configuration)
{
    uint32_t words[CONFIGURATION_RECORD_WORD_COUNT];
    decoded_record_t verified;
    bool io_error = false;
    uint32_t generation;
    uint8_t target_slot;
    size_t index;

    if ((store == NULL) || !store->initialized ||
        !product_configuration_is_valid(configuration))
    {
        if (store != NULL)
        {
            store->last_result =
                CONFIGURATION_STORE_RESULT_INVALID_ARGUMENT;
        }
        return CONFIGURATION_STORE_RESULT_INVALID_ARGUMENT;
    }
    if (store->record_valid &&
        configurations_equal(&store->configuration, configuration))
    {
        store->last_result = CONFIGURATION_STORE_RESULT_OK;
        return store->last_result;
    }

    target_slot = store->record_valid ?
        (uint8_t)(store->active_slot ^ 1u) : 0u;
    generation = store->record_valid ? store->generation + 1u : 1u;
    encode_record(configuration, generation, words);
    store->valid_slot_mask &= (uint8_t)~(1u << target_slot);
    if (!store->backend.erase_slot(store->backend.context, target_slot))
    {
        store->last_result = CONFIGURATION_STORE_RESULT_IO_ERROR;
        return store->last_result;
    }
    for (index = 0u; index < CONFIGURATION_RECORD_WORD_COUNT; ++index)
    {
        if (!store->backend.program_word(
                store->backend.context, target_slot, index, words[index]))
        {
            store->last_result = CONFIGURATION_STORE_RESULT_IO_ERROR;
            return store->last_result;
        }
    }
    if (!read_record(&store->backend,
                     target_slot,
                     &verified,
                     &io_error) ||
        io_error || (verified.generation != generation) ||
        !configurations_equal(&verified.configuration, configuration))
    {
        store->last_result = io_error ?
            CONFIGURATION_STORE_RESULT_IO_ERROR :
            CONFIGURATION_STORE_RESULT_VERIFY_ERROR;
        return store->last_result;
    }

    store->configuration = verified.configuration;
    store->generation = generation;
    store->active_slot = target_slot;
    store->valid_slot_mask |= (uint8_t)(1u << target_slot);
    store->record_valid = true;
    store->last_result = CONFIGURATION_STORE_RESULT_OK;
    return store->last_result;
}

bool configuration_store_get(
    const configuration_store_t* store,
    product_configuration_t* configuration)
{
    if ((store == NULL) || (configuration == NULL) ||
        !store->initialized || !store->record_valid)
    {
        return false;
    }
    *configuration = store->configuration;
    return true;
}

bool configuration_store_matches(
    const configuration_store_t* store,
    const product_configuration_t* configuration)
{
    return (store != NULL) && (configuration != NULL) &&
           store->initialized && store->record_valid &&
           configurations_equal(&store->configuration, configuration);
}
