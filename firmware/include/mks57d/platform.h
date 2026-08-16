#ifndef MKS57D_PLATFORM_H
#define MKS57D_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    PLATFORM_BOOT_NOT_RUN = 0,
    PLATFORM_BOOT_EARLY_READY,
    PLATFORM_BOOT_SRAM2_PARITY_ERROR,
    PLATFORM_BOOT_RESET_CLOCK_UNEXPECTED,
    PLATFORM_BOOT_MSI_TIMEOUT,
    PLATFORM_BOOT_MSI_SWITCH_TIMEOUT,
    PLATFORM_BOOT_CLOCK_VERIFY_ERROR,
    PLATFORM_BOOT_READY
} platform_boot_status_t;

typedef struct
{
    uint32_t initial_rcc_ctrl;
    uint32_t initial_rcc_cfg;
    uint32_t initial_rcc_ctrlsts;
    uint32_t reset_flags;
    uint32_t initial_sram_ctrlsts;
    uint32_t final_rcc_ctrl;
    uint32_t final_rcc_cfg;
    uint32_t final_rcc_ctrlsts;
    uint32_t final_flash_ac;
    uint32_t final_sram_ctrlsts;
    uint32_t sram2_words_initialized;
    uint32_t system_core_clock_hz;
    platform_boot_status_t status;
} platform_boot_diagnostics_t;

extern volatile platform_boot_diagnostics_t g_platform_boot_diagnostics;

bool platform_early_memory_ready(void);
platform_boot_status_t platform_clock_init(void);

#endif
