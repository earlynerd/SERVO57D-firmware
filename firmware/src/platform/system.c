#include "mks57d/platform.h"

#include <stddef.h>
#include <stdint.h>

#include "mks57d/panic.h"
#include "n32l40x.h"

_Static_assert(__FPU_PRESENT == 1u,
               "the product target requires a Cortex-M4F FPU");
_Static_assert(__FPU_USED == 1u,
               "the product target must compile for hardware floating point");

enum
{
    PLATFORM_MSI_HZ = 4000000u,
    PLATFORM_HSE_HZ = 8000000u,
    PLATFORM_SYSCLK_HZ = 64000000u,
    PLATFORM_PLL_MULTIPLIER = 8u,
    PLATFORM_AHB_HZ = PLATFORM_SYSCLK_HZ,
    PLATFORM_APB1_HZ = PLATFORM_AHB_HZ / 4u,
    PLATFORM_APB2_HZ = PLATFORM_AHB_HZ / 2u,
    PLATFORM_APB1_TIMER_HZ = PLATFORM_APB1_HZ * 2u,
    PLATFORM_APB2_TIMER_HZ = PLATFORM_APB2_HZ * 2u,
    PLATFORM_CLOCK_TIMEOUT_ITERATIONS = 65536u,
    FLASH_AC_LATENCY_MASK_TRM = 0x7u,
    FLASH_AC_LATENCY_64MHZ = 1u
};

_Static_assert(MSI_VALUE_L6 == PLATFORM_MSI_HZ,
               "Nations device header disagrees with the 4 MHz reset MSI clock");
_Static_assert(HSE_VALUE == PLATFORM_HSE_HZ,
               "Nations device header must match the board's 8 MHz crystal");
_Static_assert(RCC_CFG_SCLKSTS_MSI == 0u,
               "Nations device header disagrees with the RCC clock-source encoding");
_Static_assert(RCC_CFG_PLLMULFCT8 == 0x00180000u,
               "Nations device header disagrees with the PLL x8 encoding");

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

static const uint8_t s_apb_shift[8] = {
    0u, 0u, 0u, 0u, 1u, 2u, 3u, 4u,
};

static const uint32_t s_reset_flag_mask =
    RCC_CTRLSTS_RAMRSTF | RCC_CTRLSTS_MMURSTF | RCC_CTRLSTS_PINRSTF |
    RCC_CTRLSTS_PORRSTF | RCC_CTRLSTS_SFTRSTF | RCC_CTRLSTS_IWDGRSTF |
    RCC_CTRLSTS_WWDGRSTF | RCC_CTRLSTS_LPWRRSTF;

static uint32_t pll_source_clock_hz(uint32_t cfg)
{
    uint32_t source_hz;

    if ((cfg & RCC_CFG_PLLSRC) == RCC_CFG_PLLSRC_HSE)
    {
        source_hz = HSE_VALUE;
        if ((cfg & RCC_CFG_PLLHSEPRES) == RCC_CFG_PLLHSEPRES_HSE_DIV2)
        {
            source_hz /= 2u;
        }
    }
    else
    {
        source_hz = HSI_VALUE;
        if ((RCC->PLLHSIPRE & RCC_PLLHSIPRE_PLLHSIPRE) ==
            RCC_PLLHSIPRE_PLLHSIPRE_HSI_DIV2)
        {
            source_hz /= 2u;
        }
    }

    if ((RCC->PLLHSIPRE & RCC_PLLHSIPRE_PLLSRCDIV) ==
        RCC_PLLHSIPRE_PLLSRCDIV_ENABLE)
    {
        source_hz /= 2u;
    }
    return source_hz;
}

static uint32_t pll_multiplier(uint32_t cfg)
{
    const uint32_t low_bits =
        (cfg & (RCC_CFG_PLLMULFCT_0 | RCC_CFG_PLLMULFCT_1 |
                RCC_CFG_PLLMULFCT_2 | RCC_CFG_PLLMULFCT_3)) >> 18u;

    return (cfg & RCC_CFG_PLLMULFCT_4) != 0u ?
        low_bits + 17u : low_bits + 2u;
}

static void capture_clock_diagnostics(void)
{
    g_platform_boot_diagnostics.final_rcc_ctrl = RCC->CTRL;
    g_platform_boot_diagnostics.final_rcc_cfg = RCC->CFG;
    g_platform_boot_diagnostics.final_rcc_ctrlsts = RCC->CTRLSTS;
    g_platform_boot_diagnostics.final_flash_ac = FLASH->AC;
    g_platform_boot_diagnostics.system_core_clock_hz = SystemCoreClock;
}

static uint32_t apb_clock_hz(uint32_t prescaler_mask,
                             uint32_t prescaler_shift)
{
    const uint32_t index =
        (RCC->CFG & prescaler_mask) >> prescaler_shift;

    return index < (sizeof(s_apb_shift) / sizeof(s_apb_shift[0])) ?
        SystemCoreClock >> s_apb_shift[index] : 0u;
}

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
        source_hz = pll_source_clock_hz(cfg) * pll_multiplier(cfg);
        break;
    default:
        break;
    }

    SystemCoreClock = source_hz >> s_ahb_shift[ahb_index];
}

void SystemInit(void)
{
#if (__FPU_PRESENT == 1) && (__FPU_USED == 1)
    SCB->CPACR |= (3UL << (10u * 2u)) | (3UL << (11u * 2u));
    __DSB();
    __ISB();
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
           RCC_CFG_APB1PRES_DIV4 | RCC_CFG_APB2PRES_DIV2;
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

    /* The manual requires one wait state for 32 MHz < HCLK <= 64 MHz. */
    FLASH->AC = (FLASH->AC & ~((uint32_t)FLASH_AC_LATENCY_MASK_TRM)) |
                FLASH_AC_LATENCY_64MHZ;

    RCC->CTRL |= RCC_CTRL_HSEEN;
    timeout = PLATFORM_CLOCK_TIMEOUT_ITERATIONS;
    while (((RCC->CTRL & RCC_CTRL_HSERDF) == 0u) && (timeout != 0u))
    {
        --timeout;
    }
    if ((RCC->CTRL & RCC_CTRL_HSERDF) == 0u)
    {
        SystemCoreClockUpdate();
        capture_clock_diagnostics();
        g_platform_boot_diagnostics.status = PLATFORM_BOOT_HSE_TIMEOUT;
        return g_platform_boot_diagnostics.status;
    }

    if ((RCC->CTRL & RCC_CTRL_PLLEN) != 0u)
    {
        capture_clock_diagnostics();
        g_platform_boot_diagnostics.status =
            PLATFORM_BOOT_RESET_CLOCK_UNEXPECTED;
        return g_platform_boot_diagnostics.status;
    }

    cfg = RCC->CFG;
    cfg &= ~(RCC_CFG_PLLSRC | RCC_CFG_PLLHSEPRES |
             RCC_CFG_PLLMULFCT);
    cfg |= RCC_CFG_PLLSRC_HSE | RCC_CFG_PLLHSEPRES_HSE |
           RCC_CFG_PLLMULFCT8;
    RCC->CFG = cfg;
    RCC->PLLHSIPRE &= ~((uint32_t)RCC_PLLHSIPRE_PLLSRCDIV);

    RCC->CTRL |= RCC_CTRL_PLLEN;
    timeout = PLATFORM_CLOCK_TIMEOUT_ITERATIONS;
    while (((RCC->CTRL & RCC_CTRL_PLLRDF) == 0u) && (timeout != 0u))
    {
        --timeout;
    }
    if ((RCC->CTRL & RCC_CTRL_PLLRDF) == 0u)
    {
        SystemCoreClockUpdate();
        capture_clock_diagnostics();
        g_platform_boot_diagnostics.status = PLATFORM_BOOT_PLL_TIMEOUT;
        return g_platform_boot_diagnostics.status;
    }

    cfg = RCC->CFG;
    cfg &= ~RCC_CFG_SCLKSW;
    cfg |= RCC_CFG_SCLKSW_PLL;
    RCC->CFG = cfg;

    timeout = PLATFORM_CLOCK_TIMEOUT_ITERATIONS;
    while (((RCC->CFG & RCC_CFG_SCLKSTS) != RCC_CFG_SCLKSTS_PLL) &&
           (timeout != 0u))
    {
        --timeout;
    }
    if ((RCC->CFG & RCC_CFG_SCLKSTS) != RCC_CFG_SCLKSTS_PLL)
    {
        SystemCoreClockUpdate();
        capture_clock_diagnostics();
        g_platform_boot_diagnostics.status = PLATFORM_BOOT_PLL_SWITCH_TIMEOUT;
        return g_platform_boot_diagnostics.status;
    }

    SystemCoreClockUpdate();
    capture_clock_diagnostics();

    if ((SystemCoreClock != PLATFORM_SYSCLK_HZ) ||
        ((RCC->CTRL & (RCC_CTRL_HSEEN | RCC_CTRL_HSERDF |
                       RCC_CTRL_PLLEN | RCC_CTRL_PLLRDF)) !=
         (RCC_CTRL_HSEEN | RCC_CTRL_HSERDF |
          RCC_CTRL_PLLEN | RCC_CTRL_PLLRDF)) ||
        ((RCC->CFG & (RCC_CFG_SCLKSTS | RCC_CFG_AHBPRES |
                      RCC_CFG_APB1PRES | RCC_CFG_APB2PRES |
                      RCC_CFG_PLLSRC | RCC_CFG_PLLHSEPRES |
                      RCC_CFG_PLLMULFCT)) !=
         (RCC_CFG_SCLKSTS_PLL | RCC_CFG_AHBPRES_DIV1 |
          RCC_CFG_APB1PRES_DIV4 | RCC_CFG_APB2PRES_DIV2 |
          RCC_CFG_PLLSRC_HSE | RCC_CFG_PLLHSEPRES_HSE |
          RCC_CFG_PLLMULFCT8)) ||
        ((FLASH->AC & FLASH_AC_LATENCY_MASK_TRM) !=
         FLASH_AC_LATENCY_64MHZ) ||
        (platform_apb1_clock_hz() != PLATFORM_APB1_HZ) ||
        (platform_apb2_clock_hz() != PLATFORM_APB2_HZ) ||
        (platform_apb1_timer_clock_hz() != PLATFORM_APB1_TIMER_HZ) ||
        (platform_apb2_timer_clock_hz() != PLATFORM_APB2_TIMER_HZ))
    {
        g_platform_boot_diagnostics.status = PLATFORM_BOOT_CLOCK_VERIFY_ERROR;
        return g_platform_boot_diagnostics.status;
    }

    g_platform_boot_diagnostics.status = PLATFORM_BOOT_READY;
    return g_platform_boot_diagnostics.status;
}

uint32_t platform_apb1_clock_hz(void)
{
    return apb_clock_hz(RCC_CFG_APB1PRES, 8u);
}

uint32_t platform_apb2_clock_hz(void)
{
    return apb_clock_hz(RCC_CFG_APB2PRES, 11u);
}

uint32_t platform_apb1_timer_clock_hz(void)
{
    const uint32_t peripheral_hz = platform_apb1_clock_hz();

    return (RCC->CFG & RCC_CFG_APB1PRES) == RCC_CFG_APB1PRES_DIV1 ?
        peripheral_hz : peripheral_hz * 2u;
}

uint32_t platform_apb2_timer_clock_hz(void)
{
    const uint32_t peripheral_hz = platform_apb2_clock_hz();

    return (RCC->CFG & RCC_CFG_APB2PRES) == RCC_CFG_APB2PRES_DIV1 ?
        peripheral_hz : peripheral_hz * 2u;
}
