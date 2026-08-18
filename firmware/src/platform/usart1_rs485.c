#include "mks57d/rs485.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mks57d/dma_channels.h"
#include "mks57d/dma_ring.h"
#include "mks57d/interrupt_priority.h"
#include "n32l40x.h"

enum
{
    RS485_DIRECTION_PIN = 13u,
    RS485_TX_PIN = 9u,
    RS485_RX_PIN = 10u,
    RS485_DIRECTION_MASK = 1u << RS485_DIRECTION_PIN,
    RS485_DIRECTION_MODE_MASK = 3u << (RS485_DIRECTION_PIN * 2u),
    RS485_DIRECTION_MODE_OUTPUT = 1u << (RS485_DIRECTION_PIN * 2u),
    RS485_DIRECTION_DRIVE_4MA = 2u << (RS485_DIRECTION_PIN * 2u),
    RS485_USART_GPIO_PIN_MASK = (1u << RS485_TX_PIN) |
                                (1u << RS485_RX_PIN),
    RS485_USART_GPIO_MODE_MASK = (3u << (RS485_TX_PIN * 2u)) |
                                 (3u << (RS485_RX_PIN * 2u)),
    RS485_USART_GPIO_MODE_AF = (2u << (RS485_TX_PIN * 2u)) |
                               (2u << (RS485_RX_PIN * 2u)),
    RS485_USART_GPIO_DRIVE_4MA = 2u << (RS485_TX_PIN * 2u),
    RS485_GPIO_AF_MASK = (15u << ((RS485_TX_PIN - 8u) * 4u)) |
                         (15u << ((RS485_RX_PIN - 8u) * 4u)),
    RS485_GPIO_AF4 = (4u << ((RS485_TX_PIN - 8u) * 4u)) |
                     (4u << ((RS485_RX_PIN - 8u) * 4u)),
    DMA_REQUEST_USART1_TX = 1u,
    DMA_REQUEST_USART1_RX = 2u,
    DMA_CFG_ENABLE = 1u << 0,
    DMA_CFG_TRANSFER_COMPLETE_INTERRUPT = 1u << 1,
    DMA_CFG_HALF_TRANSFER_INTERRUPT = 1u << 2,
    DMA_CFG_TRANSFER_ERROR_INTERRUPT = 1u << 3,
    DMA_CFG_MEMORY_TO_PERIPHERAL = 1u << 4,
    DMA_CFG_CIRCULAR = 1u << 5,
    DMA_CFG_MEMORY_INCREMENT = 1u << 7,
    DMA_CFG_PRIORITY_MEDIUM = 1u << 12,
    DMA_RX_CONFIGURATION = DMA_CFG_TRANSFER_COMPLETE_INTERRUPT |
                           DMA_CFG_HALF_TRANSFER_INTERRUPT |
                           DMA_CFG_TRANSFER_ERROR_INTERRUPT |
                           DMA_CFG_CIRCULAR |
                           DMA_CFG_MEMORY_INCREMENT |
                           DMA_CFG_PRIORITY_MEDIUM,
    DMA_TX_CONFIGURATION = DMA_CFG_TRANSFER_COMPLETE_INTERRUPT |
                           DMA_CFG_TRANSFER_ERROR_INTERRUPT |
                           DMA_CFG_MEMORY_TO_PERIPHERAL |
                           DMA_CFG_MEMORY_INCREMENT |
                           DMA_CFG_PRIORITY_MEDIUM,
    DMA_CHANNEL4_ALL_INTERRUPT_FLAGS =
        DMA_INTCLR_CGLBF4 | DMA_INTCLR_CTXCF4 |
        DMA_INTCLR_CHTXF4 | DMA_INTCLR_CERRF4,
    DMA_CHANNEL5_ALL_INTERRUPT_FLAGS =
        DMA_INTCLR_CGLBF5 | DMA_INTCLR_CTXCF5 |
        DMA_INTCLR_CHTXF5 | DMA_INTCLR_CERRF5,
    USART_ERROR_MASK = USART_STS_PEF | USART_STS_FEF |
                       USART_STS_NEF | USART_STS_OREF,
    NVIC_PRIORITY_SHIFT = 8u - __NVIC_PRIO_BITS
};

_Static_assert(DMA_CHANNEL_USART1_RX == 4u,
               "USART1 RX IRQ and flag mapping requires DMA channel 4");
_Static_assert(DMA_CHANNEL_USART1_TX == 5u,
               "USART1 TX IRQ and flag mapping requires DMA channel 5");
_Static_assert((RS485_RX_DMA_BUFFER_SIZE &
                (RS485_RX_DMA_BUFFER_SIZE - 1u)) == 0u,
               "RS-485 RX DMA buffer must be a power of two");
_Static_assert(RS485_RX_DMA_BUFFER_SIZE <= UINT16_MAX,
               "DMA transfer count is 16-bit");
_Static_assert(RS485_TX_MAX_FRAME_SIZE <= UINT16_MAX,
               "DMA transfer count is 16-bit");

static volatile uint8_t s_rx_dma_buffer[RS485_RX_DMA_BUFFER_SIZE];
static uint8_t s_tx_dma_buffer[RS485_TX_MAX_FRAME_SIZE];
static dma_ring_cursor_t s_rx_cursor;
static volatile uint32_t s_rx_dma_wrap_count;
static volatile uint32_t s_rx_idle_events;
static volatile uint32_t s_rx_error_count;
static volatile uint32_t s_tx_bytes;
static volatile uint32_t s_tx_frame_count;
static volatile uint32_t s_tx_error_count;
static volatile uint32_t s_tx_length;
static volatile uint32_t s_tx_busy;
static volatile rs485_status_t s_status = RS485_STATUS_NOT_READY;
static bool s_initialized;

static uint32_t communications_critical_enter(void)
{
    const uint32_t previous = __get_BASEPRI();
    const uint32_t threshold =
        (uint32_t)INTERRUPT_PRIORITY_COMMUNICATIONS << NVIC_PRIORITY_SHIFT;

    __set_BASEPRI_MAX(threshold);
    __DSB();
    __ISB();
    return previous;
}

static void communications_critical_exit(uint32_t previous)
{
    __set_BASEPRI(previous);
    __DSB();
    __ISB();
}

static uint32_t rx_produced_total(void)
{
    uint32_t wraps;
    uint32_t remaining;
    const uint32_t previous_basepri = communications_critical_enter();

    wraps = s_rx_dma_wrap_count;
    remaining = DMA_CH4->TXNUM;

    /* DMA continues while its IRQ is masked. Account for the one completion
       which can become pending during this bounded register snapshot. */
    if (((DMA->INTSTS & DMA_INTSTS_TXCF4) != 0u) && (remaining != 0u))
    {
        ++wraps;
    }
    communications_critical_exit(previous_basepri);

    if (remaining > RS485_RX_DMA_BUFFER_SIZE)
    {
        remaining = RS485_RX_DMA_BUFFER_SIZE;
    }

    return (wraps * RS485_RX_DMA_BUFFER_SIZE) +
           (RS485_RX_DMA_BUFFER_SIZE - remaining);
}

static bool baud_divider(uint32_t peripheral_clock_hz,
                         uint16_t* divider)
{
    uint32_t rounded_divider;
    uint32_t actual_product;
    uint32_t absolute_error;

    if ((divider == NULL) ||
        (peripheral_clock_hz < (RS485_BAUD_RATE * 16u)))
    {
        return false;
    }

    rounded_divider =
        (peripheral_clock_hz + (RS485_BAUD_RATE / 2u)) /
        RS485_BAUD_RATE;
    if ((rounded_divider < 16u) || (rounded_divider > UINT16_MAX))
    {
        return false;
    }

    actual_product = rounded_divider * RS485_BAUD_RATE;
    absolute_error = (actual_product > peripheral_clock_hz) ?
        (actual_product - peripheral_clock_hz) :
        (peripheral_clock_hz - actual_product);
    if ((absolute_error * 100u) > (peripheral_clock_hz * 2u))
    {
        return false;
    }

    *divider = (uint16_t)rounded_divider;
    return true;
}

static void force_receive_mode(void)
{
    GPIOC->PBC = (uint32_t)RS485_DIRECTION_MASK;
}

static void fail_transport(rs485_status_t status)
{
    DMA_CH4->CHCFG &= ~((uint32_t)DMA_CFG_ENABLE);
    DMA_CH5->CHCFG &= ~((uint32_t)DMA_CFG_ENABLE);
    USART1->CTRL3 &= (uint16_t)~(USART_CTRL3_DMARXEN |
                                  USART_CTRL3_DMATXEN |
                                  USART_CTRL3_ERRIEN);
    USART1->CTRL1 &= (uint16_t)~(USART_CTRL1_IDLEIEN |
                                  USART_CTRL1_TXCIEN);
    force_receive_mode();
    s_tx_busy = 0u;
    s_status = status;
    s_initialized = false;
}

static bool dma_configuration_readback_ok(void)
{
    return ((USART1->CTRL3 & (USART_CTRL3_DMARXEN |
                             USART_CTRL3_DMATXEN)) ==
                (USART_CTRL3_DMARXEN | USART_CTRL3_DMATXEN)) &&
           (DMA_CH4->CHSEL == DMA_REQUEST_USART1_RX) &&
           ((DMA_CH4->CHCFG & DMA_CFG_ENABLE) != 0u) &&
           (DMA_CH5->CHSEL == DMA_REQUEST_USART1_TX);
}

static bool configuration_readback_ok(uint16_t divider)
{
    return ((GPIOA->PMODE & (uint32_t)RS485_USART_GPIO_MODE_MASK) ==
                (uint32_t)RS485_USART_GPIO_MODE_AF) &&
           ((GPIOC->PMODE & (uint32_t)RS485_DIRECTION_MODE_MASK) ==
                (uint32_t)RS485_DIRECTION_MODE_OUTPUT) &&
           ((GPIOA->AFH & (uint32_t)RS485_GPIO_AF_MASK) ==
                (uint32_t)RS485_GPIO_AF4) &&
           ((GPIOC->POD & (uint32_t)RS485_DIRECTION_MASK) == 0u) &&
           (USART1->BRCF == divider) &&
           ((USART1->CTRL1 & (USART_CTRL1_UEN |
                              USART_CTRL1_RXEN |
                              USART_CTRL1_TXEN)) ==
                (USART_CTRL1_UEN | USART_CTRL1_RXEN |
                 USART_CTRL1_TXEN)) &&
           dma_configuration_readback_ok();
}

rs485_status_t rs485_init(uint32_t peripheral_clock_hz)
{
    uint16_t divider;

    if (s_initialized)
    {
        return RS485_STATUS_OK;
    }
    if (!baud_divider(peripheral_clock_hz, &divider))
    {
        s_status = RS485_STATUS_CONFIGURATION_ERROR;
        return s_status;
    }

    RCC->APB2PCLKEN |= RCC_APB2PCLKEN_AFIOEN |
                       RCC_APB2PCLKEN_IOPAEN |
                       RCC_APB2PCLKEN_IOPCEN |
                       RCC_APB2PCLKEN_USART1EN;
    RCC->AHBPCLKEN |= RCC_AHBPCLKEN_DMAEN;
    __DSB();

    /* Preload receive-low before PC13 becomes an output. */
    GPIOC->POD &= ~((uint32_t)RS485_DIRECTION_MASK);
    GPIOC->POTYPE &= ~((uint32_t)RS485_DIRECTION_MASK);
    GPIOC->PUPD &= ~((uint32_t)RS485_DIRECTION_MODE_MASK);
    GPIOC->DS = (GPIOC->DS & ~((uint32_t)RS485_DIRECTION_MODE_MASK)) |
                (uint32_t)RS485_DIRECTION_DRIVE_4MA;
    *((volatile uint16_t*)&GPIOC->SR) |=
        (uint16_t)RS485_DIRECTION_MASK;
    GPIOC->PMODE =
        (GPIOC->PMODE & ~((uint32_t)RS485_DIRECTION_MODE_MASK)) |
        (uint32_t)RS485_DIRECTION_MODE_OUTPUT;

    GPIOA->POTYPE &= ~((uint32_t)RS485_USART_GPIO_PIN_MASK);
    GPIOA->PUPD &= ~((uint32_t)RS485_USART_GPIO_MODE_MASK);
    GPIOA->DS = (GPIOA->DS & ~((uint32_t)RS485_USART_GPIO_MODE_MASK)) |
                (uint32_t)RS485_USART_GPIO_DRIVE_4MA;
    *((volatile uint16_t*)&GPIOA->SR) |=
        (uint16_t)RS485_USART_GPIO_PIN_MASK;
    GPIOA->AFH = (GPIOA->AFH & ~((uint32_t)RS485_GPIO_AF_MASK)) |
                 (uint32_t)RS485_GPIO_AF4;
    GPIOA->PMODE =
        (GPIOA->PMODE & ~((uint32_t)RS485_USART_GPIO_MODE_MASK)) |
        (uint32_t)RS485_USART_GPIO_MODE_AF;
    force_receive_mode();

    if (((DMA_CH4->CHCFG | DMA_CH5->CHCFG) & DMA_CFG_ENABLE) != 0u)
    {
        s_status = RS485_STATUS_BUSY;
        return s_status;
    }

    NVIC_DisableIRQ(DMA_Channel4_IRQn);
    NVIC_DisableIRQ(DMA_Channel5_IRQn);
    NVIC_DisableIRQ(USART1_IRQn);

    RCC->APB2PRST |= RCC_APB2PRST_USART1RST;
    RCC->APB2PRST &= ~RCC_APB2PRST_USART1RST;

    DMA_CH4->CHCFG = 0u;
    DMA_CH5->CHCFG = 0u;
    DMA->INTCLR = DMA_CHANNEL4_ALL_INTERRUPT_FLAGS |
                  DMA_CHANNEL5_ALL_INTERRUPT_FLAGS;

    s_rx_dma_wrap_count = 0u;
    s_rx_idle_events = 0u;
    s_rx_error_count = 0u;
    s_tx_bytes = 0u;
    s_tx_frame_count = 0u;
    s_tx_error_count = 0u;
    s_tx_length = 0u;
    s_tx_busy = 0u;
    dma_ring_cursor_init(&s_rx_cursor, 0u);

    DMA_CH4->PADDR = (uint32_t)(uintptr_t)&USART1->DAT;
    DMA_CH4->MADDR = (uint32_t)(uintptr_t)s_rx_dma_buffer;
    DMA_CH4->TXNUM = RS485_RX_DMA_BUFFER_SIZE;
    DMA_CH4->CHSEL = DMA_REQUEST_USART1_RX;
    DMA_CH4->CHCFG = DMA_RX_CONFIGURATION;

    DMA_CH5->PADDR = (uint32_t)(uintptr_t)&USART1->DAT;
    DMA_CH5->MADDR = (uint32_t)(uintptr_t)s_tx_dma_buffer;
    DMA_CH5->TXNUM = 0u;
    DMA_CH5->CHSEL = DMA_REQUEST_USART1_TX;
    DMA_CH5->CHCFG = DMA_TX_CONFIGURATION;

    USART1->CTRL1 = 0u;
    USART1->CTRL2 = 0u;
    USART1->CTRL3 = 0u;
    USART1->BRCF = divider;
    (void)USART1->STS;
    (void)USART1->DAT;

    NVIC_ClearPendingIRQ(DMA_Channel4_IRQn);
    NVIC_ClearPendingIRQ(DMA_Channel5_IRQn);
    NVIC_ClearPendingIRQ(USART1_IRQn);
    NVIC_SetPriority(DMA_Channel4_IRQn,
                     INTERRUPT_PRIORITY_COMMUNICATIONS);
    NVIC_SetPriority(DMA_Channel5_IRQn,
                     INTERRUPT_PRIORITY_COMMUNICATIONS);
    NVIC_SetPriority(USART1_IRQn,
                     INTERRUPT_PRIORITY_COMMUNICATIONS);
    NVIC_EnableIRQ(DMA_Channel4_IRQn);
    NVIC_EnableIRQ(DMA_Channel5_IRQn);
    NVIC_EnableIRQ(USART1_IRQn);

    /* Match the Nations USART/DMA examples: leave both peripheral DMA
       requests enabled and gate each transfer with its DMA channel. */
    USART1->CTRL3 = USART_CTRL3_DMARXEN | USART_CTRL3_DMATXEN |
                    USART_CTRL3_ERRIEN;
    DMA_CH4->CHCFG = DMA_RX_CONFIGURATION | DMA_CFG_ENABLE;
    USART1->CTRL1 = USART_CTRL1_UEN | USART_CTRL1_RXEN |
                    USART_CTRL1_TXEN | USART_CTRL1_IDLEIEN;
    __DSB();

    if (!configuration_readback_ok(divider))
    {
        fail_transport(RS485_STATUS_CONFIGURATION_ERROR);
        return s_status;
    }

    s_status = RS485_STATUS_OK;
    s_initialized = true;
    return RS485_STATUS_OK;
}

size_t rs485_read(uint8_t* destination, size_t capacity)
{
    uint32_t produced;

    if (!s_initialized || ((destination == NULL) && (capacity != 0u)))
    {
        return 0u;
    }

    produced = rx_produced_total();
    __DMB();
    return dma_ring_copy(&s_rx_cursor,
                         s_rx_dma_buffer,
                         RS485_RX_DMA_BUFFER_SIZE,
                         produced,
                         destination,
                         capacity);
}

rs485_status_t rs485_write(const uint8_t* bytes, size_t length)
{
    size_t index;

    if ((bytes == NULL) || (length == 0u) ||
        (length > RS485_TX_MAX_FRAME_SIZE))
    {
        return RS485_STATUS_INVALID_ARGUMENT;
    }
    if (!s_initialized || (s_status != RS485_STATUS_OK))
    {
        return RS485_STATUS_NOT_READY;
    }
    if (s_tx_busy != 0u)
    {
        return RS485_STATUS_BUSY;
    }

    for (index = 0u; index < length; ++index)
    {
        s_tx_dma_buffer[index] = bytes[index];
    }

    s_tx_length = (uint32_t)length;
    s_tx_busy = 1u;
    DMA_CH5->CHCFG = DMA_TX_CONFIGURATION;
    DMA->INTCLR = DMA_CHANNEL5_ALL_INTERRUPT_FLAGS;
    DMA_CH5->MADDR = (uint32_t)(uintptr_t)s_tx_dma_buffer;
    DMA_CH5->TXNUM = (uint32_t)length;
    USART1->CTRL1 &= (uint16_t)~USART_CTRL1_TXCIEN;

    /* N32L40x clears TXC with an STS read followed by a DAT write. Prime
       that documented sequence here; the first DMA request supplies the
       actual first frame byte to DAT without emitting a dummy byte. */
    (void)USART1->STS;

    /* The SP485E driver is enabled before channel 5 can write the first byte.
       Its 70 ns maximum enable time is shorter than this register path. */
    GPIOC->PBSC = (uint32_t)RS485_DIRECTION_MASK;
    __DMB();
    DMA_CH5->CHCFG = DMA_TX_CONFIGURATION | DMA_CFG_ENABLE;
    return RS485_STATUS_OK;
}

void rs485_get_stats(rs485_stats_t* stats)
{
    if (stats == NULL)
    {
        return;
    }

    stats->status = (uint32_t)s_status;
    stats->rx_bytes = rx_produced_total();
    stats->rx_idle_events = s_rx_idle_events;
    stats->rx_error_count = s_rx_error_count;
    stats->rx_overrun_count = s_rx_cursor.overrun_count;
    stats->rx_dropped_bytes = s_rx_cursor.dropped_bytes;
    stats->tx_bytes = s_tx_bytes;
    stats->tx_frame_count = s_tx_frame_count;
    stats->tx_error_count = s_tx_error_count;
    stats->tx_busy = s_tx_busy;
}

void DMA_Channel4_IRQHandler(void)
{
    const uint32_t flags = DMA->INTSTS;

    if ((flags & DMA_INTSTS_ERRF4) != 0u)
    {
        DMA->INTCLR = DMA_CHANNEL4_ALL_INTERRUPT_FLAGS;
        ++s_rx_error_count;
        fail_transport(RS485_STATUS_DMA_ERROR);
        return;
    }
    if ((flags & DMA_INTSTS_TXCF4) != 0u)
    {
        DMA->INTCLR = DMA_INTCLR_CTXCF4 | DMA_INTCLR_CGLBF4;
        ++s_rx_dma_wrap_count;
    }
    if ((flags & DMA_INTSTS_HTXF4) != 0u)
    {
        DMA->INTCLR = DMA_INTCLR_CHTXF4 | DMA_INTCLR_CGLBF4;
    }
}

void DMA_Channel5_IRQHandler(void)
{
    const uint32_t flags = DMA->INTSTS;

    if ((flags & DMA_INTSTS_ERRF5) != 0u)
    {
        DMA->INTCLR = DMA_CHANNEL5_ALL_INTERRUPT_FLAGS;
        ++s_tx_error_count;
        fail_transport(RS485_STATUS_DMA_ERROR);
        return;
    }
    if ((flags & DMA_INTSTS_TXCF5) != 0u)
    {
        DMA->INTCLR = DMA_INTCLR_CTXCF5 | DMA_INTCLR_CGLBF5;
        DMA_CH5->CHCFG = DMA_TX_CONFIGURATION;

        /* DMA completion means only that DAT accepted the last byte. Keep the
           transceiver driving until USART TXC proves the shifter is empty. */
        USART1->CTRL1 |= USART_CTRL1_TXCIEN;
    }
}

void USART1_IRQHandler(void)
{
    const uint16_t status = USART1->STS;

    if ((status & (USART_ERROR_MASK | USART_STS_IDLEF)) != 0u)
    {
        /* STS then DAT is the documented clear sequence for receive errors
           and IDLE. RX byte movement remains owned by DMA. */
        (void)USART1->DAT;
        if ((status & USART_ERROR_MASK) != 0u)
        {
            ++s_rx_error_count;
        }
        if ((status & USART_STS_IDLEF) != 0u)
        {
            ++s_rx_idle_events;
        }
    }

    if (((status & USART_STS_TXC) != 0u) &&
        ((USART1->CTRL1 & USART_CTRL1_TXCIEN) != 0u))
    {
        USART1->CTRL1 &= (uint16_t)~USART_CTRL1_TXCIEN;

        /* TXC is not write-to-clear on this USART. Leave it latched with
           TXCIEN disabled; the next STS-read/DAT-write transmit sequence
           clears it without adding a byte to the bus. */
        force_receive_mode();
        s_tx_bytes += s_tx_length;
        ++s_tx_frame_count;
        s_tx_length = 0u;
        s_tx_busy = 0u;
    }
}
