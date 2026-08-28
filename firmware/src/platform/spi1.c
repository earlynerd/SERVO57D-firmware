#include "mks57d/spi1.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mks57d/board.h"
#include "mks57d/deferred_deadline_indicator.h"
#include "mks57d/dma_channels.h"
#include "mks57d/current_loop_backend.h"
#include "mks57d/cycle_counter.h"
#include "mks57d/interrupt_priority.h"
#include "mks57d/runtime_profile.h"
#include "mks57d/timebase.h"
#include "n32l40x.h"

enum
{
    SPI_GPIO_SCK_PIN = 3u,
    SPI_GPIO_MISO_PIN = 4u,
    SPI_GPIO_MOSI_PIN = 5u,
    SPI_GPIO_CS_PIN = 6u,
    SPI_GPIO_SCK_MASK = 1u << SPI_GPIO_SCK_PIN,
    SPI_GPIO_MISO_MASK = 1u << SPI_GPIO_MISO_PIN,
    SPI_GPIO_MOSI_MASK = 1u << SPI_GPIO_MOSI_PIN,
    SPI_GPIO_CS_MASK = 1u << SPI_GPIO_CS_PIN,
    SPI_GPIO_OUTPUT_MASK = SPI_GPIO_SCK_MASK |
                           SPI_GPIO_MOSI_MASK |
                           SPI_GPIO_CS_MASK,
    SPI_GPIO_MODE_MASK = (3u << (SPI_GPIO_SCK_PIN * 2u)) |
                         (3u << (SPI_GPIO_MISO_PIN * 2u)) |
                         (3u << (SPI_GPIO_MOSI_PIN * 2u)) |
                         (3u << (SPI_GPIO_CS_PIN * 2u)),
    SPI_GPIO_MODE_VALUE = (2u << (SPI_GPIO_SCK_PIN * 2u)) |
                          (2u << (SPI_GPIO_MOSI_PIN * 2u)) |
                          (1u << (SPI_GPIO_CS_PIN * 2u)),
    SPI_GPIO_PULLUP_VALUE = 1u << (SPI_GPIO_SCK_PIN * 2u),
    SPI_GPIO_DRIVE_4MA = (2u << (SPI_GPIO_SCK_PIN * 2u)) |
                         (2u << (SPI_GPIO_MOSI_PIN * 2u)) |
                         (2u << (SPI_GPIO_CS_PIN * 2u)),
    SPI_GPIO_AF_MASK = (15u << (SPI_GPIO_SCK_PIN * 4u)) |
                       (15u << (SPI_GPIO_MISO_PIN * 4u)) |
                       (15u << (SPI_GPIO_MOSI_PIN * 4u)),
    SPI_GPIO_AF_VALUE = (1u << (SPI_GPIO_SCK_PIN * 4u)) |
                        (1u << (SPI_GPIO_MISO_PIN * 4u)),
    SPI_POLL_BUDGET = 8192u,
    SPI_SUPPORTED_CLOCK_MIN_HZ = 1000000u,
    SPI_SUPPORTED_CLOCK_MAX_HZ = 64000000u,
    SPI_ERROR_MASK = SPI_STS_MODERR | SPI_STS_OVER,
    SPI_DMA_REQUEST_TX = 0x0Du,
    SPI_DMA_REQUEST_RX = 0x0Eu,
    SPI_DMA_CFG_ENABLE = 1u << 0,
    SPI_DMA_CFG_TRANSFER_COMPLETE_INTERRUPT = 1u << 1,
    SPI_DMA_CFG_TRANSFER_ERROR_INTERRUPT = 1u << 3,
    SPI_DMA_CFG_MEMORY_TO_PERIPHERAL = 1u << 4,
    SPI_DMA_CFG_MEMORY_INCREMENT = 1u << 7,
    SPI_DMA_CFG_PRIORITY_HIGH = 2u << 12,
    SPI_DMA_RX_CONFIGURATION =
        SPI_DMA_CFG_TRANSFER_COMPLETE_INTERRUPT |
        SPI_DMA_CFG_TRANSFER_ERROR_INTERRUPT |
        SPI_DMA_CFG_MEMORY_INCREMENT |
        SPI_DMA_CFG_PRIORITY_HIGH,
    SPI_DMA_TX_CONFIGURATION =
        SPI_DMA_CFG_TRANSFER_ERROR_INTERRUPT |
        SPI_DMA_CFG_MEMORY_TO_PERIPHERAL |
        SPI_DMA_CFG_MEMORY_INCREMENT,
    SPI_DMA_CHANNEL2_ALL_INTERRUPT_FLAGS =
        DMA_INTCLR_CGLBF2 | DMA_INTCLR_CTXCF2 |
        DMA_INTCLR_CHTXF2 | DMA_INTCLR_CERRF2,
    SPI_DMA_CHANNEL3_ALL_INTERRUPT_FLAGS =
        DMA_INTCLR_CGLBF3 | DMA_INTCLR_CTXCF3 |
        DMA_INTCLR_CHTXF3 | DMA_INTCLR_CERRF3,
    SPI_PERIODIC_TIMER_TICK_HZ = 1000000u,
    SPI_PERIODIC_INTERVAL_US =
        SPI_PERIODIC_TIMER_TICK_HZ / SPI1_PERIODIC_FREQUENCY_HZ,
    SPI_CHIP_SELECT_GUARD_US = 2u,
    NVIC_PRIORITY_SHIFT = 8u - __NVIC_PRIO_BITS
};

typedef enum
{
    SPI_PERIODIC_STATE_STOPPED = 0,
    SPI_PERIODIC_STATE_IDLE,
    SPI_PERIODIC_STATE_CS_SETUP,
    SPI_PERIODIC_STATE_TRANSFER,
    SPI_PERIODIC_STATE_CS_HOLD
} spi_periodic_state_t;

_Static_assert(DMA_CHANNEL_ENCODER_RX == 2u,
               "SPI1 RX IRQ and flag mapping requires DMA channel 2");
_Static_assert(DMA_CHANNEL_ENCODER_TX == 3u,
               "SPI1 TX IRQ and flag mapping requires DMA channel 3");
_Static_assert((SPI_PERIODIC_TIMER_TICK_HZ %
                SPI1_PERIODIC_FREQUENCY_HZ) == 0u,
               "periodic SPI timer requires an integral interval");

static bool s_spi1_initialized;
static volatile spi_periodic_state_t s_periodic_state =
    SPI_PERIODIC_STATE_STOPPED;
static uint8_t s_periodic_transmit[SPI1_MAX_TRANSFER_BYTES];
static volatile uint8_t s_periodic_receive[SPI1_MAX_TRANSFER_BYTES];
static uint8_t s_deferred_receive[SPI1_MAX_TRANSFER_BYTES];
static size_t s_periodic_length;
static spi1_periodic_exchange_callback_t s_periodic_callback;
static void* s_periodic_callback_context;
static volatile spi_status_t s_deferred_status = SPI_STATUS_NOT_READY;
static volatile uint32_t s_deferred_timestamp_us;
static volatile uint32_t s_deferred_pending;
static volatile uint32_t s_periodic_acquisition_timestamp_us;
static volatile bool s_periodic_acquisition_timestamp_valid;
static bool s_periodic_first_release;
static bool s_periodic_prime_pending;
static volatile uint32_t s_periodic_release_sequence;
static volatile uint32_t s_deferred_release_sequence;
static volatile deferred_deadline_indicator_t s_deadline_indicator;

static void periodic_exchange_stop(void);

static uint32_t rotor_feedback_critical_enter(void)
{
    const uint32_t previous = __get_BASEPRI();
    const uint32_t threshold =
        (uint32_t)INTERRUPT_PRIORITY_ROTOR_FEEDBACK << NVIC_PRIORITY_SHIFT;

    __set_BASEPRI_MAX(threshold);
    __DSB();
    __ISB();
    return previous;
}

static void rotor_feedback_critical_exit(uint32_t previous)
{
    __set_BASEPRI(previous);
    __DSB();
    __ISB();
}

static void deferred_release_complete(uint32_t release_sequence)
{
    s_deadline_indicator.latest_completed_sequence = release_sequence;
    __DMB();

    if (s_deadline_indicator.active)
    {
        const uint32_t previous = rotor_feedback_critical_enter();

        if (deferred_deadline_indicator_complete(
                &s_deadline_indicator, release_sequence))
        {
            board_status_led_set(false);
        }
        rotor_feedback_critical_exit(previous);
    }
}

static uint16_t baud_rate_bits(uint32_t peripheral_clock_hz)
{
    uint32_t divisor = 2u;
    uint16_t bits = 0u;

    while ((peripheral_clock_hz > (SPI1_TARGET_CLOCK_HZ * divisor)) &&
           (divisor < 256u))
    {
        divisor <<= 1u;
        bits = (uint16_t)(bits + SPI_CTRL1_BR0);
    }

    return bits;
}

static void clear_receive_and_overrun(void)
{
    uint32_t remaining = SPI_POLL_BUDGET;

    while (((SPI1->STS & SPI_STS_RNE) != 0u) && (remaining != 0u))
    {
        (void)SPI1->DAT;
        --remaining;
    }
    if ((SPI1->STS & SPI_STS_OVER) != 0u)
    {
        (void)SPI1->DAT;
        (void)SPI1->STS;
    }
}

static void periodic_chip_select_high(void)
{
    GPIOB->PBSC = (uint32_t)SPI_GPIO_CS_MASK;
}

static void periodic_dma_disable(void)
{
    DMA_CH2->CHCFG &= ~((uint32_t)SPI_DMA_CFG_ENABLE);
    DMA_CH3->CHCFG &= ~((uint32_t)SPI_DMA_CFG_ENABLE);
    SPI1->CTRL2 &= (uint16_t)~(SPI_CTRL2_RDMAEN |
                               SPI_CTRL2_TDMAEN);
}

static bool periodic_receive_path_clear(void)
{
    if ((SPI1->STS & SPI_STS_RNE) != 0u)
    {
        (void)SPI1->DAT;
    }
    if ((SPI1->STS & SPI_STS_OVER) != 0u)
    {
        (void)SPI1->DAT;
        (void)SPI1->STS;
    }
    return (SPI1->STS & (SPI_STS_RNE | SPI_ERROR_MASK)) == 0u;
}

static void periodic_timer7_start(uint16_t microseconds)
{
    TIM7->CTRL1 = 0u;
    TIM7->DINTEN = 0u;
    TIM7->CNT = 0u;
    TIM7->AR = (uint16_t)(microseconds - 1u);
    TIM7->EVTGEN = TIM_EVTGEN_UDGN;
    __DSB();
    TIM7->STS = 0u;
    NVIC_ClearPendingIRQ(TIM7_IRQn);
    __DSB();
    TIM7->DINTEN = TIM_DINTEN_UIEN;
    TIM7->CTRL1 = TIM_CTRL1_ONEPM | TIM_CTRL1_CNTEN;
}

static uint32_t periodic_acquisition_timestamp_or_now(void)
{
    return s_periodic_acquisition_timestamp_valid ?
        s_periodic_acquisition_timestamp_us : timebase_micros();
}

static void periodic_publish(spi_status_t status, uint32_t timestamp_us)
{
    size_t index;

    s_periodic_acquisition_timestamp_us = 0u;
    s_periodic_acquisition_timestamp_valid = false;

    /* The N32L40x DMA has demonstrated a one-transfer startup anomaly on both
       ADC and SPI paths. Exercise and discard exactly one bounded exchange
       before defining the production sample stream. Any later error is
       published normally and remains visible to rotor-control policy. */
    if (s_periodic_prime_pending)
    {
        s_periodic_prime_pending = false;
        deferred_release_complete(s_periodic_release_sequence);
        return;
    }

    if (s_deferred_pending != 0u)
    {
        status = SPI_STATUS_BUS_BUSY;
    }
    for (index = 0u; index < s_periodic_length; ++index)
    {
        s_deferred_receive[index] = s_periodic_receive[index];
    }
    s_deferred_status = status;
    s_deferred_timestamp_us = timestamp_us;
    s_deferred_release_sequence = s_periodic_release_sequence;
    __DMB();
    s_deferred_pending = 1u;
    if (runtime_profile_is_armed())
    {
        runtime_profile_deferred_pended(cycle_counter_read());
    }
    SCB->ICSR = SCB_ICSR_PENDSVSET_Msk;

}

static void periodic_fail_transfer(spi_status_t status, uint32_t timestamp_us)
{
    periodic_dma_disable();
    DMA->INTCLR = SPI_DMA_CHANNEL2_ALL_INTERRUPT_FLAGS |
                  SPI_DMA_CHANNEL3_ALL_INTERRUPT_FLAGS;
    TIM7->CTRL1 = 0u;
    TIM7->STS = 0u;
    periodic_chip_select_high();
    s_periodic_state = SPI_PERIODIC_STATE_IDLE;
    periodic_publish(status, timestamp_us);
}

static void periodic_begin_dma(void)
{
    if ((SPI1->STS & SPI_ERROR_MASK) != 0u)
    {
        periodic_fail_transfer(
            SPI_STATUS_PERIPHERAL_ERROR,
            periodic_acquisition_timestamp_or_now());
        return;
    }

    DMA_CH2->CHCFG = SPI_DMA_RX_CONFIGURATION;
    DMA_CH3->CHCFG = SPI_DMA_TX_CONFIGURATION;
    DMA->INTCLR = SPI_DMA_CHANNEL2_ALL_INTERRUPT_FLAGS |
                  SPI_DMA_CHANNEL3_ALL_INTERRUPT_FLAGS;
    DMA_CH2->MADDR = (uint32_t)(uintptr_t)s_periodic_receive;
    DMA_CH2->TXNUM = (uint32_t)s_periodic_length;
    DMA_CH3->MADDR = (uint32_t)(uintptr_t)s_periodic_transmit;
    DMA_CH3->TXNUM = (uint32_t)s_periodic_length;
    s_periodic_state = SPI_PERIODIC_STATE_TRANSFER;
    __DMB();
    DMA_CH2->CHCFG = SPI_DMA_RX_CONFIGURATION | SPI_DMA_CFG_ENABLE;
    DMA_CH3->CHCFG = SPI_DMA_TX_CONFIGURATION | SPI_DMA_CFG_ENABLE;
    __DSB();
    SPI1->CTRL2 |= SPI_CTRL2_RDMAEN | SPI_CTRL2_TDMAEN;
}

bool spi1_init(uint32_t peripheral_clock_hz)
{
    uint16_t control;

    s_spi1_initialized = false;
    s_periodic_state = SPI_PERIODIC_STATE_STOPPED;

    if ((peripheral_clock_hz < SPI_SUPPORTED_CLOCK_MIN_HZ) ||
        (peripheral_clock_hz > SPI_SUPPORTED_CLOCK_MAX_HZ))
    {
        return false;
    }

    RCC->APB2PCLKEN |= RCC_APB2PCLKEN_AFIOEN |
                       RCC_APB2PCLKEN_IOPBEN |
                       RCC_APB2PCLKEN_SPI1EN;
    __DSB();

    /* Establish the mode-3 idle levels before the peripheral can drive SCK.
       The N32L40x erratum requires a pull-up before enabling CPOL-high SPI so
       the encoder cannot interpret the enable transition as a clock edge. */
    GPIOB->POD |= (uint32_t)(SPI_GPIO_SCK_MASK | SPI_GPIO_CS_MASK);
    GPIOB->POTYPE &= ~((uint32_t)SPI_GPIO_OUTPUT_MASK);
    GPIOB->PUPD = (GPIOB->PUPD & ~((uint32_t)SPI_GPIO_MODE_MASK)) |
                  (uint32_t)SPI_GPIO_PULLUP_VALUE;
    GPIOB->DS = (GPIOB->DS & ~((uint32_t)SPI_GPIO_MODE_MASK)) |
                (uint32_t)SPI_GPIO_DRIVE_4MA;
    *((volatile uint16_t*)&GPIOB->SR) |= (uint16_t)SPI_GPIO_OUTPUT_MASK;
    /* Exact-board Nations mapping: PB3/PB4 use AF1 while PB5 uses AF0. */
    GPIOB->AFL = (GPIOB->AFL & ~((uint32_t)SPI_GPIO_AF_MASK)) |
                 (uint32_t)SPI_GPIO_AF_VALUE;
    GPIOB->PMODE = (GPIOB->PMODE & ~((uint32_t)SPI_GPIO_MODE_MASK)) |
                   (uint32_t)SPI_GPIO_MODE_VALUE;

    RCC->APB2PRST |= RCC_APB2PRST_SPI1RST;
    RCC->APB2PRST &= ~RCC_APB2PRST_SPI1RST;

    SPI1->CTRL1 = 0u;
    SPI1->CTRL2 = 0u;
    SPI1->I2SCFG &= (uint16_t)~SPI_I2SCFG_MODSEL;
    SPI1->CRCPOLY = 7u;

    control = SPI_CTRL1_CLKPOL |
              SPI_CTRL1_CLKPHA |
              SPI_CTRL1_MSEL |
              SPI_CTRL1_SSEL |
              SPI_CTRL1_SSMEN |
              baud_rate_bits(peripheral_clock_hz);
    SPI1->CTRL1 = control;
    SPI1->CTRL1 = control | SPI_CTRL1_SPIEN;

    clear_receive_and_overrun();
    if ((SPI1->STS & SPI_STS_MODERR) != 0u)
    {
        return false;
    }

    s_spi1_initialized = true;
    return true;
}

bool spi1_periodic_exchange_start(
    const uint8_t* transmit,
    size_t length,
    uint32_t timer_clock_hz,
    uint32_t initial_delay_millis,
    spi1_periodic_exchange_callback_t callback,
    void* callback_context)
{
    size_t index;
    uint32_t initial_delay_us;

    if (!s_spi1_initialized || (transmit == NULL) ||
        (length == 0u) || (length > SPI1_MAX_TRANSFER_BYTES) ||
        (callback == NULL) || (initial_delay_millis == 0u) ||
        (initial_delay_millis > (UINT16_MAX / 1000u)) ||
        (timer_clock_hz < SPI_PERIODIC_TIMER_TICK_HZ) ||
        ((timer_clock_hz % SPI_PERIODIC_TIMER_TICK_HZ) != 0u))
    {
        return false;
    }
    initial_delay_us = initial_delay_millis * 1000u;
    if ((initial_delay_us == 0u) || (initial_delay_us > UINT16_MAX))
    {
        return false;
    }
    if (s_periodic_state != SPI_PERIODIC_STATE_STOPPED)
    {
        return false;
    }

    for (index = 0u; index < length; ++index)
    {
        s_periodic_transmit[index] = transmit[index];
        s_periodic_receive[index] = 0u;
        s_deferred_receive[index] = 0u;
    }
    s_periodic_length = length;
    s_periodic_callback = callback;
    s_periodic_callback_context = callback_context;
    s_deferred_status = SPI_STATUS_NOT_READY;
    s_deferred_timestamp_us = 0u;
    s_deferred_pending = 0u;
    s_periodic_acquisition_timestamp_us = 0u;
    s_periodic_acquisition_timestamp_valid = false;
    s_periodic_first_release = true;
    s_periodic_prime_pending = true;
    s_periodic_release_sequence = 0u;
    s_deferred_release_sequence = 0u;
    deferred_deadline_indicator_init(&s_deadline_indicator);
    board_status_led_set(false);

    RCC->AHBPCLKEN |= RCC_AHBPCLKEN_DMAEN;
    RCC->APB1PCLKEN |= RCC_APB1PCLKEN_TIM6EN |
                       RCC_APB1PCLKEN_TIM7EN;
    __DSB();
    RCC->APB1PRST |= RCC_APB1PRST_TIM6RST |
                     RCC_APB1PRST_TIM7RST;
    RCC->APB1PRST &= ~(RCC_APB1PRST_TIM6RST |
                       RCC_APB1PRST_TIM7RST);

    periodic_dma_disable();
    DMA_CH2->CHCFG = 0u;
    DMA_CH3->CHCFG = 0u;
    DMA->INTCLR = SPI_DMA_CHANNEL2_ALL_INTERRUPT_FLAGS |
                  SPI_DMA_CHANNEL3_ALL_INTERRUPT_FLAGS;
    DMA_CH2->PADDR = (uint32_t)(uintptr_t)&SPI1->DAT;
    DMA_CH2->CHSEL = SPI_DMA_REQUEST_RX;
    DMA_CH3->PADDR = (uint32_t)(uintptr_t)&SPI1->DAT;
    DMA_CH3->CHSEL = SPI_DMA_REQUEST_TX;

    TIM6->CTRL1 = 0u;
    TIM6->DINTEN = 0u;
    TIM6->PSC = (uint16_t)(timer_clock_hz /
                           SPI_PERIODIC_TIMER_TICK_HZ - 1u);
    TIM6->AR = (uint16_t)(initial_delay_us - 1u);
    TIM6->CNT = 0u;
    TIM6->EVTGEN = TIM_EVTGEN_UDGN;
    __DSB();
    TIM6->STS = 0u;

    TIM7->CTRL1 = 0u;
    TIM7->DINTEN = 0u;
    TIM7->PSC = TIM6->PSC;
    TIM7->AR = SPI_CHIP_SELECT_GUARD_US - 1u;
    TIM7->CNT = 0u;
    TIM7->EVTGEN = TIM_EVTGEN_UDGN;
    __DSB();
    TIM7->STS = 0u;

    NVIC_DisableIRQ(DMA_Channel2_IRQn);
    NVIC_DisableIRQ(DMA_Channel3_IRQn);
    NVIC_DisableIRQ(TIM6_IRQn);
    NVIC_DisableIRQ(TIM7_IRQn);
    NVIC_ClearPendingIRQ(DMA_Channel2_IRQn);
    NVIC_ClearPendingIRQ(DMA_Channel3_IRQn);
    NVIC_ClearPendingIRQ(TIM6_IRQn);
    NVIC_ClearPendingIRQ(TIM7_IRQn);
    __DSB();
    TIM6->DINTEN = TIM_DINTEN_UIEN;
    TIM7->DINTEN = TIM_DINTEN_UIEN;
    NVIC_SetPriority(DMA_Channel2_IRQn, INTERRUPT_PRIORITY_ROTOR_FEEDBACK);
    NVIC_SetPriority(DMA_Channel3_IRQn, INTERRUPT_PRIORITY_ROTOR_FEEDBACK);
    NVIC_SetPriority(TIM6_IRQn, INTERRUPT_PRIORITY_ROTOR_FEEDBACK);
    NVIC_SetPriority(TIM7_IRQn, INTERRUPT_PRIORITY_ROTOR_FEEDBACK);
    NVIC_SetPriority(PendSV_IRQn, INTERRUPT_PRIORITY_SLOW_RELEASE);
    NVIC_EnableIRQ(DMA_Channel2_IRQn);
    NVIC_EnableIRQ(DMA_Channel3_IRQn);
    NVIC_EnableIRQ(TIM6_IRQn);
    NVIC_EnableIRQ(TIM7_IRQn);

    periodic_chip_select_high();
    s_periodic_state = SPI_PERIODIC_STATE_IDLE;
    TIM6->CTRL1 = TIM_CTRL1_CNTEN;
    __DSB();
    if ((DMA_CH2->CHSEL != SPI_DMA_REQUEST_RX) ||
        (DMA_CH3->CHSEL != SPI_DMA_REQUEST_TX) ||
        (TIM6->PSC != (uint16_t)(timer_clock_hz /
                                  SPI_PERIODIC_TIMER_TICK_HZ - 1u)) ||
        (TIM6->DINTEN != TIM_DINTEN_UIEN) ||
        (TIM7->DINTEN != TIM_DINTEN_UIEN) ||
        ((TIM6->CTRL1 & TIM_CTRL1_CNTEN) == 0u) ||
        (NVIC_GetPriority(DMA_Channel2_IRQn) !=
             INTERRUPT_PRIORITY_ROTOR_FEEDBACK) ||
        (NVIC_GetPriority(DMA_Channel3_IRQn) !=
             INTERRUPT_PRIORITY_ROTOR_FEEDBACK) ||
        (NVIC_GetPriority(TIM6_IRQn) !=
             INTERRUPT_PRIORITY_ROTOR_FEEDBACK) ||
        (NVIC_GetPriority(TIM7_IRQn) !=
             INTERRUPT_PRIORITY_ROTOR_FEEDBACK) ||
        (NVIC_GetPriority(PendSV_IRQn) !=
             INTERRUPT_PRIORITY_SLOW_RELEASE))
    {
        periodic_exchange_stop();
        return false;
    }
    return true;
}

static void periodic_exchange_stop(void)
{
    TIM6->CTRL1 = 0u;
    TIM7->CTRL1 = 0u;
    periodic_dma_disable();
    periodic_chip_select_high();
    s_periodic_state = SPI_PERIODIC_STATE_STOPPED;
    s_deferred_pending = 0u;
    s_periodic_acquisition_timestamp_us = 0u;
    s_periodic_acquisition_timestamp_valid = false;
    s_periodic_prime_pending = false;
    deferred_deadline_indicator_init(&s_deadline_indicator);
    board_status_led_set(false);
}

void TIM6_IRQHandler(void)
{
    TIM6->STS = 0u;
    if (s_periodic_state == SPI_PERIODIC_STATE_STOPPED)
    {
        return;
    }
    if (s_periodic_first_release)
    {
        TIM6->AR = SPI_PERIODIC_INTERVAL_US - 1u;
        s_periodic_first_release = false;
    }
    if (deferred_deadline_indicator_deadline_elapsed(
            &s_deadline_indicator))
    {
        board_status_led_set(true);
    }
    if (s_periodic_state != SPI_PERIODIC_STATE_IDLE)
    {
        periodic_fail_transfer(SPI_STATUS_BUS_BUSY, timebase_micros());
        return;
    }

    s_periodic_release_sequence =
        deferred_deadline_indicator_start(&s_deadline_indicator);

    if (!periodic_receive_path_clear())
    {
        periodic_fail_transfer(
            SPI_STATUS_PERIPHERAL_ERROR, timebase_micros());
        return;
    }
    /* Capture immediately before asserting CS so higher-priority preemption
       cannot make the coherent acquisition window appear newer than it is. */
    s_periodic_acquisition_timestamp_us = timebase_micros();
    s_periodic_acquisition_timestamp_valid = true;
    GPIOB->PBC = (uint32_t)SPI_GPIO_CS_MASK;
    s_periodic_state = SPI_PERIODIC_STATE_CS_SETUP;
    periodic_timer7_start(SPI_CHIP_SELECT_GUARD_US);
}

void TIM7_IRQHandler(void)
{
    TIM7->STS = 0u;
    if (s_periodic_state == SPI_PERIODIC_STATE_CS_SETUP)
    {
        periodic_begin_dma();
    }
    else if (s_periodic_state == SPI_PERIODIC_STATE_CS_HOLD)
    {
        const spi_status_t status =
            ((SPI1->STS & (SPI_ERROR_MASK | SPI_STS_BUSY)) == 0u) ?
                SPI_STATUS_OK : SPI_STATUS_PERIPHERAL_ERROR;

        periodic_chip_select_high();
        s_periodic_state = SPI_PERIODIC_STATE_IDLE;
        periodic_publish(
            status, periodic_acquisition_timestamp_or_now());
    }
    else if (s_periodic_state != SPI_PERIODIC_STATE_STOPPED)
    {
        periodic_fail_transfer(
            SPI_STATUS_PERIPHERAL_ERROR,
            periodic_acquisition_timestamp_or_now());
    }
}

void DMA_Channel2_IRQHandler(void)
{
    const uint32_t flags = DMA->INTSTS;

    if ((flags & DMA_INTSTS_ERRF2) != 0u)
    {
        DMA->INTCLR = SPI_DMA_CHANNEL2_ALL_INTERRUPT_FLAGS;
        periodic_fail_transfer(
            SPI_STATUS_PERIPHERAL_ERROR,
            periodic_acquisition_timestamp_or_now());
        return;
    }
    if ((flags & DMA_INTSTS_TXCF2) != 0u)
    {
        DMA->INTCLR = DMA_INTCLR_CTXCF2 | DMA_INTCLR_CGLBF2;
        periodic_dma_disable();
        s_periodic_state = SPI_PERIODIC_STATE_CS_HOLD;
        periodic_timer7_start(SPI_CHIP_SELECT_GUARD_US);
    }
}

void DMA_Channel3_IRQHandler(void)
{
    const uint32_t flags = DMA->INTSTS;

    if ((flags & DMA_INTSTS_ERRF3) != 0u)
    {
        DMA->INTCLR = SPI_DMA_CHANNEL3_ALL_INTERRUPT_FLAGS;
        periodic_fail_transfer(
            SPI_STATUS_PERIPHERAL_ERROR,
            periodic_acquisition_timestamp_or_now());
    }
    else if ((flags & DMA_INTSTS_TXCF3) != 0u)
    {
        DMA->INTCLR = DMA_INTCLR_CTXCF3 | DMA_INTCLR_CGLBF3;
    }
}

void PendSV_Handler(void)
{
    uint8_t receive[SPI1_MAX_TRANSFER_BYTES];
    spi1_periodic_exchange_callback_t callback;
    void* callback_context;
    spi_status_t status;
    uint32_t timestamp_us;
    uint32_t release_sequence;
    size_t index;
    bool profile_active = false;

    if (s_deferred_pending == 0u)
    {
        return;
    }
    if (runtime_profile_is_armed())
    {
        profile_active = runtime_profile_pendsv_begin(
            cycle_counter_read(),
            current_loop_backend_sample_count());
    }
    status = s_deferred_status;
    timestamp_us = s_deferred_timestamp_us;
    release_sequence = s_deferred_release_sequence;
    callback = s_periodic_callback;
    callback_context = s_periodic_callback_context;
    for (index = 0u; index < s_periodic_length; ++index)
    {
        receive[index] = s_deferred_receive[index];
    }
    __DMB();
    s_deferred_pending = 0u;
    if (callback != NULL)
    {
        if (profile_active)
        {
            runtime_profile_callback_begin(cycle_counter_read());
        }
        callback(callback_context,
                 status,
                 receive,
                 s_periodic_length,
                 timestamp_us);
    }
    if (profile_active)
    {
        runtime_profile_pendsv_complete(
            cycle_counter_read(),
            current_loop_backend_sample_count());
    }
    deferred_release_complete(release_sequence);
}
