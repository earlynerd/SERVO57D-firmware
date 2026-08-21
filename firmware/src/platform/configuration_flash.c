#include "mks57d/configuration_flash.h"

#include <stddef.h>
#include <stdint.h>

#include "n32l40x.h"

enum
{
    FLASH_UNLOCK_KEY1 = 0x45670123u,
    FLASH_UNLOCK_KEY2 = 0xCDEF89ABu,
    FLASH_STATUS_ECC_ERROR = 1u << 7,
    FLASH_STATUS_ERROR_MASK = FLASH_STS_PGERR | FLASH_STS_PVERR |
                              FLASH_STS_WRPERR | FLASH_STS_EVERR |
                              FLASH_STATUS_ECC_ERROR,
    FLASH_STATUS_CLEAR_MASK = FLASH_STATUS_ERROR_MASK | FLASH_STS_EOP,
    FLASH_OPERATION_TIMEOUT_ITERATIONS = 8000000u,
    FLASH_HSI_TIMEOUT_ITERATIONS = 1000000u,
    CONFIGURATION_FLASH_WORDS_PER_SLOT =
        CONFIGURATION_STORE_PAGE_SIZE_BYTES / sizeof(uint32_t)
};

_Static_assert(
    (CONFIGURATION_STORE_PAGE_SIZE_BYTES % sizeof(uint32_t)) == 0u,
    "configuration Flash page must contain whole program words");

extern const uint32_t __configuration_slot0_start__;
extern const uint32_t __configuration_slot1_start__;
extern const uint32_t __configuration_slot1_end__;

static uintptr_t slot_base(uint8_t slot)
{
    if (slot == 0u)
    {
        return (uintptr_t)&__configuration_slot0_start__;
    }
    if (slot == 1u)
    {
        return (uintptr_t)&__configuration_slot1_start__;
    }
    return 0u;
}

static bool wait_for_hsi(void)
{
    uint32_t remaining = FLASH_HSI_TIMEOUT_ITERATIONS;

    while (((RCC->CTRL & RCC_CTRL_HSIRDF) == 0u) && (remaining != 0u))
    {
        --remaining;
    }
    return (RCC->CTRL & RCC_CTRL_HSIRDF) != 0u;
}

static bool wait_for_flash(void)
{
    uint32_t remaining = FLASH_OPERATION_TIMEOUT_ITERATIONS;

    while (((FLASH->STS & FLASH_STS_BUSY) != 0u) && (remaining != 0u))
    {
        --remaining;
    }
    return (FLASH->STS & FLASH_STS_BUSY) == 0u;
}

static bool begin_operation(bool* hsi_was_enabled)
{
    *hsi_was_enabled = (RCC->CTRL & RCC_CTRL_HSIEN) != 0u;
    if (!*hsi_was_enabled)
    {
        RCC->CTRL |= RCC_CTRL_HSIEN;
        if (!wait_for_hsi())
        {
            RCC->CTRL &= ~((uint32_t)RCC_CTRL_HSIEN);
            return false;
        }
    }
    if (!wait_for_flash())
    {
        if (!*hsi_was_enabled)
        {
            RCC->CTRL &= ~((uint32_t)RCC_CTRL_HSIEN);
        }
        return false;
    }
    if ((FLASH->CTRL & FLASH_CTRL_LOCK) != 0u)
    {
        FLASH->KEYR = FLASH_UNLOCK_KEY1;
        FLASH->KEYR = FLASH_UNLOCK_KEY2;
    }
    if ((FLASH->CTRL & FLASH_CTRL_LOCK) != 0u)
    {
        if (!*hsi_was_enabled)
        {
            RCC->CTRL &= ~((uint32_t)RCC_CTRL_HSIEN);
        }
        return false;
    }

    FLASH->CTRL &= ~((uint32_t)(FLASH_CTRL_PG | FLASH_CTRL_PER |
                                FLASH_CTRL_MER | FLASH_CTRL_OPTPG |
                                FLASH_CTRL_OPTER | FLASH_CTRL_SMPSEL));
    FLASH->STS = FLASH_STATUS_CLEAR_MASK;
    return true;
}

static void end_operation(bool hsi_was_enabled)
{
    FLASH->CTRL &= ~((uint32_t)(FLASH_CTRL_PG | FLASH_CTRL_PER |
                                FLASH_CTRL_MER | FLASH_CTRL_OPTPG |
                                FLASH_CTRL_OPTER));
    FLASH->CTRL |= FLASH_CTRL_LOCK;
    if (!hsi_was_enabled)
    {
        RCC->CTRL &= ~((uint32_t)RCC_CTRL_HSIEN);
    }
}

static bool flash_read_word(void* context,
                            uint8_t slot,
                            size_t word_index,
                            uint32_t* value)
{
    const uintptr_t base = slot_base(slot);

    (void)context;
    if ((base == 0u) ||
        (word_index >= CONFIGURATION_FLASH_WORDS_PER_SLOT) ||
        (value == NULL))
    {
        return false;
    }
    *value = *(const volatile uint32_t*)(
        base + word_index * sizeof(uint32_t));
    return true;
}

static bool flash_erase_slot(void* context, uint8_t slot)
{
    const uintptr_t base = slot_base(slot);
    uint32_t saved_primask;
    uint32_t status;
    size_t index;
    bool hsi_was_enabled = false;
    bool success = false;

    (void)context;
    if ((base == 0u) ||
        ((base % CONFIGURATION_STORE_PAGE_SIZE_BYTES) != 0u))
    {
        return false;
    }

    saved_primask = __get_PRIMASK();
    __disable_irq();
    if (!begin_operation(&hsi_was_enabled))
    {
        goto done;
    }
    if ((FLASH->CTRL & FLASH_CTRL_START) != 0u)
    {
        goto finish;
    }
    FLASH->CTRL |= FLASH_CTRL_PER;
    FLASH->ADD = (uint32_t)base;
    FLASH->CTRL |= FLASH_CTRL_START;
    __DSB();
    if (!wait_for_flash())
    {
        goto finish;
    }
    status = FLASH->STS;
    if (((status & FLASH_STATUS_ERROR_MASK) != 0u) ||
        ((status & FLASH_STS_EOP) == 0u))
    {
        goto finish;
    }
    FLASH->STS = FLASH_STATUS_CLEAR_MASK;
    success = true;
    for (index = 0u; index < CONFIGURATION_FLASH_WORDS_PER_SLOT; ++index)
    {
        if (*(const volatile uint32_t*)(
                base + index * sizeof(uint32_t)) != UINT32_MAX)
        {
            success = false;
            break;
        }
    }

finish:
    end_operation(hsi_was_enabled);
done:
    if (saved_primask == 0u)
    {
        __enable_irq();
    }
    return success;
}

static bool flash_program_word(void* context,
                               uint8_t slot,
                               size_t word_index,
                               uint32_t value)
{
    const uintptr_t base = slot_base(slot);
    const uintptr_t address = base + word_index * sizeof(uint32_t);
    uint32_t saved_primask;
    uint32_t status;
    bool hsi_was_enabled = false;
    bool success = false;

    (void)context;
    if ((base == 0u) ||
        (word_index >= CONFIGURATION_FLASH_WORDS_PER_SLOT) ||
        (*(const volatile uint32_t*)address != UINT32_MAX))
    {
        return false;
    }

    saved_primask = __get_PRIMASK();
    __disable_irq();
    if (!begin_operation(&hsi_was_enabled))
    {
        goto done;
    }
    if ((FLASH->CTRL & FLASH_CTRL_START) != 0u)
    {
        goto finish;
    }
    FLASH->CTRL |= FLASH_CTRL_PG;
    *(volatile uint32_t*)address = value;
    __NOP();
    __NOP();
    __NOP();
    __NOP();
    if (!wait_for_flash())
    {
        goto finish;
    }
    status = FLASH->STS;
    if (((status & FLASH_STATUS_ERROR_MASK) == 0u) &&
        ((status & FLASH_STS_EOP) != 0u) &&
        (*(const volatile uint32_t*)address == value))
    {
        success = true;
    }
    FLASH->STS = FLASH_STATUS_CLEAR_MASK;

finish:
    end_operation(hsi_was_enabled);
done:
    if (saved_primask == 0u)
    {
        __enable_irq();
    }
    return success;
}

bool configuration_flash_backend_init(
    configuration_store_backend_t* backend)
{
    const uintptr_t slot0 = (uintptr_t)&__configuration_slot0_start__;
    const uintptr_t slot1 = (uintptr_t)&__configuration_slot1_start__;
    const uintptr_t slot1_end = (uintptr_t)&__configuration_slot1_end__;

    if ((backend == NULL) ||
        (slot0 != 0x0801F000u) ||
        (slot1 != 0x0801F800u) ||
        (slot1_end != 0x08020000u) ||
        ((slot1 - slot0) != CONFIGURATION_STORE_PAGE_SIZE_BYTES) ||
        ((slot1_end - slot1) != CONFIGURATION_STORE_PAGE_SIZE_BYTES))
    {
        return false;
    }
    backend->context = NULL;
    backend->read_word = flash_read_word;
    backend->erase_slot = flash_erase_slot;
    backend->program_word = flash_program_word;
    return true;
}
