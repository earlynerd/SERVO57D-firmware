#include "mks57d/platform.h"

#include <stddef.h>
#include <stdint.h>

#include "mks57d/panic.h"
#include "n32l40x.h"

enum
{
    PLATFORM_MSI_HZ = 4000000u,
    PLATFORM_CLOCK_TIMEOUT_ITERATIONS = 65536u,
    FLASH_AC_LATENCY_MASK_TRM = 0x7u
};

_Static_assert(MSI_VALUE_L6 == PLATFORM_MSI_HZ,
               "Nations device header disagrees with the 4 MHz reset MSI clock");
_Static_assert(RCC_CFG_SCLKSTS_MSI == 0u,
               "Nations device header disagrees with the RCC clock-source encoding");

extern uint32_t __sram2_start__;
extern uint32_t __sram2_end__;

uint32_t SystemCoreClock = PLATFORM_MSI_HZ;

volatile platform_boot_diagnostics_t g_platform_boot_diagnostics;

static const uint32_t s_msi_clock_hz[7] = {
    MSI_VALUE_L0,
    MSI_VALUE_L1,
    MSI_VALUE_L2,
    MSI_VALUE_L3,
    MSI_VALUE_L4,
    MSI_VALUE_L5,
    MSI_VALUE_L6,
};

static const uint8_t s_ahb_shift[16] = {
    0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
    1u, 2u, 3u, 4u, 6u, 7u, 8u, 9u,
};

static const uint32_t s_reset_flag_mask =
    RCC_CTRLSTS_RAMRSTF | RCC_CTRLSTS_MMURSTF | RCC_CTRLSTS_PINRSTF |
    RCC_CTRLSTS_PORRSTF | RCC_CTRLSTS_SFTRSTF | RCC_CTRLSTS_IWDGRSTF |
    RCC_CTRLSTS_WWDGRSTF | RCC_CTRLSTS_LPWRRSTF;

static void initialize_sram2(void)
{
    volatile uint32_t* word = &__sram2_start__;
    volatile uint32_t* const end = &__sram2_end__;
    uint32_t count = 0u;

    /* Store-only initialization establishes valid parity for every SRAM2 word. */
    while (word < end)
    {
        *word = 0u;
        ++word;
        ++count;
    }

    __DSB();

    /* ERR2STS is write-one-to-clear. Leave the reset interrupt/reset policy alone. */
    RCC->SRAM_CTRLSTS = RCC_SRAM_CTRLSTS_ERR2STS;
    __DSB();

    g_platform_boot_diagnostics.sram2_words_initialized = count;
    g_platform_boot_diagnostics.final_sram_ctrlsts = RCC->SRAM_CTRLSTS;
}

void SystemCoreClockUpdate(void)
{
    const uint32_t cfg = RCC->CFG;
    const uint32_t source = cfg & RCC_CFG_SCLKSTS;
    const uint32_t ahb_index = (cfg & RCC_CFG_AHBPRES) >> 4;
    uint32_t source_hz = 0u;

    switch (source)
    {
    case RCC_CFG_SCLKSTS_MSI:
    {
        const uint32_t range = (RCC->CTRLSTS & RCC_CTRLSTS_MSIRANGE) >> 4;
        if (range < (sizeof(s_msi_clock_hz) / sizeof(s_msi_clock_hz[0])))
        {
            source_hz = s_msi_clock_hz[range];
        }
        break;
    }
    case RCC_CFG_SCLKSTS_HSI:
        source_hz = HSI_VALUE;
        break;
    case RCC_CFG_SCLKSTS_HSE:
        source_hz = HSE_VALUE;
        break;
    case RCC_CFG_SCLKSTS_PLL:
    default:
        /* PLL decoding is intentionally deferred until the 64 MHz path is proven. */
        source_hz = 0u;
        break;
    }

    SystemCoreClock = source_hz >> s_ahb_shift[ahb_index];
}

void SystemInit(void)
{
#if (__FPU_PRESENT == 1) && (__FPU_USED == 1)
    SCB->CPACR |= (3UL << (10u * 2u)) | (3UL << (11u * 2u));
#endif

    g_platform_boot_diagnostics.initial_rcc_ctrl = RCC->CTRL;
    g_platform_boot_diagnostics.initial_rcc_cfg = RCC->CFG;
    g_platform_boot_diagnostics.initial_rcc_ctrlsts = RCC->CTRLSTS;
    g_platform_boot_diagnostics.reset_flags =
        g_platform_boot_diagnostics.initial_rcc_ctrlsts & s_reset_flag_mask;
    g_platform_boot_diagnostics.initial_sram_ctrlsts = RCC->SRAM_CTRLSTS;

    if (((g_platform_boot_diagnostics.reset_flags &
          RCC_CTRLSTS_IWDGRSTF) == 0u) ||
        ((uint32_t)g_last_panic >= (uint32_t)PANIC_CODE_COUNT))
    {
        g_last_panic = PANIC_NONE;
    }

    /* Snapshot first, then clear the sticky flags for an unambiguous next boot. */
    RCC->CTRLSTS |= RCC_CTRLSTS_RMRSTF;
    __DSB();

    SCB->VTOR = FLASH_BASE;
    __DSB();
    __ISB();

    initialize_sram2();
    SystemCoreClockUpdate();
    g_platform_boot_diagnostics.system_core_clock_hz = SystemCoreClock;

    if ((g_platform_boot_diagnostics.final_sram_ctrlsts &
         RCC_SRAM_CTRLSTS_ERR2STS) != 0u)
    {
        g_platform_boot_diagnostics.status = PLATFORM_BOOT_SRAM2_PARITY_ERROR;
    }
    else
    {
        g_platform_boot_diagnostics.status = PLATFORM_BOOT_EARLY_READY;
    }
}

bool platform_early_memory_ready(void)
{
    return g_platform_boot_diagnostics.status == PLATFORM_BOOT_EARLY_READY;
}

platform_boot_status_t platform_clock_init(void)
{
    uint32_t timeout;
    uint32_t cfg;

    if (!platform_early_memory_ready())
    {
        return g_platform_boot_diagnostics.status;
    }

    if (((RCC->CFG & RCC_CFG_SCLKSTS) != RCC_CFG_SCLKSTS_MSI) ||
        ((RCC->CTRLSTS & RCC_CTRLSTS_MSIRANGE) != RCC_CTRLSTS_MSIRANGE_4MHz))
    {
        g_platform_boot_diagnostics.status = PLATFORM_BOOT_RESET_CLOCK_UNEXPECTED;
        return g_platform_boot_diagnostics.status;
    }

    RCC->CTRLSTS |= RCC_CTRLSTS_MSIEN;
    timeout = PLATFORM_CLOCK_TIMEOUT_ITERATIONS;
    while (((RCC->CTRLSTS & RCC_CTRLSTS_MSIRD) == 0u) && (timeout != 0u))
    {
        --timeout;
    }
    if ((RCC->CTRLSTS & RCC_CTRLSTS_MSIRD) == 0u)
    {
        g_platform_boot_diagnostics.status = PLATFORM_BOOT_MSI_TIMEOUT;
        return g_platform_boot_diagnostics.status;
    }

    cfg = RCC->CFG;
    cfg &= ~(RCC_CFG_SCLKSW | RCC_CFG_AHBPRES |
             RCC_CFG_APB1PRES | RCC_CFG_APB2PRES);
    cfg |= RCC_CFG_SCLKSW_MSI | RCC_CFG_AHBPRES_DIV1 |
           RCC_CFG_APB1PRES_DIV1 | RCC_CFG_APB2PRES_DIV1;
    RCC->CFG = cfg;

    timeout = PLATFORM_CLOCK_TIMEOUT_ITERATIONS;
    while (((RCC->CFG & RCC_CFG_SCLKSTS) != RCC_CFG_SCLKSTS_MSI) &&
           (timeout != 0u))
    {
        --timeout;
    }
    if ((RCC->CFG & RCC_CFG_SCLKSTS) != RCC_CFG_SCLKSTS_MSI)
    {
        g_platform_boot_diagnostics.status = PLATFORM_BOOT_MSI_SWITCH_TIMEOUT;
        return g_platform_boot_diagnostics.status;
    }

    /* The TRM permits zero flash wait states at SYSCLK <= 32 MHz. */
    FLASH->AC &= ~((uint32_t)FLASH_AC_LATENCY_MASK_TRM);
    SystemCoreClockUpdate();

    g_platform_boot_diagnostics.final_rcc_ctrl = RCC->CTRL;
    g_platform_boot_diagnostics.final_rcc_cfg = RCC->CFG;
    g_platform_boot_diagnostics.final_rcc_ctrlsts = RCC->CTRLSTS;
    g_platform_boot_diagnostics.final_flash_ac = FLASH->AC;
    g_platform_boot_diagnostics.system_core_clock_hz = SystemCoreClock;

    if ((SystemCoreClock != PLATFORM_MSI_HZ) ||
        ((RCC->CFG & (RCC_CFG_AHBPRES | RCC_CFG_APB1PRES |
                      RCC_CFG_APB2PRES)) != 0u) ||
        ((FLASH->AC & FLASH_AC_LATENCY_MASK_TRM) != 0u))
    {
        g_platform_boot_diagnostics.status = PLATFORM_BOOT_CLOCK_VERIFY_ERROR;
        return g_platform_boot_diagnostics.status;
    }

    g_platform_boot_diagnostics.status = PLATFORM_BOOT_READY;
    return g_platform_boot_diagnostics.status;
}
