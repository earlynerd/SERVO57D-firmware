#include "mks57d/adc1.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mks57d/dma_channels.h"
#include "mks57d/interrupt_priority.h"
#include "mks57d/tim2_current_trigger.h"
#include "n32l40x.h"

enum
{
    ADC_GPIO_MODE_MASK = (3u << (1u * 2u)) |
                         (3u << (2u * 2u)) |
                         (3u << (3u * 2u)),
    ADC_GPIO_MODE_ANALOG = ADC_GPIO_MODE_MASK,
    ADC_SAMPLE_TIME_7CYCLES5 = 1u,
    ADC_SAMPLE_TIME_28CYCLES5 = 3u,
    ADC_SAMPLE_TIME_55CYCLES5 = 5u,
    ADC_CTRL3_RESOLUTION_12BIT = 3u,
    ADC_CTRL3_READY = 1u << 5u,
    ADC_STATUS_WRITABLE_MASK = 0x7Fu,
    ADC_SOFTWARE_TRIGGER_SELECT = ADC_CTRL2_EXTRSEL,
    ADC_SOFTWARE_START = ADC_CTRL2_EXTRTRIG | ADC_CTRL2_SWSTRRCH,
    ADC_POLL_BUDGET = 20000u,
    ADC_LDO_CONTROL_OFFSET = 0x60u,
    ADC_LDO_ENABLE_VALUE = 0x28u,
    ADC_CURRENT_DMA_BUFFER_LENGTH = 2u,
    ADC_CURRENT_SEQUENCE_LENGTH = 2u,
    ADC_CURRENT_SEQUENCE_LENGTH_ENCODING =
        ADC_CURRENT_SEQUENCE_LENGTH - 1u,
    ADC_INJECTED_VBUS_RANK_SHIFT = 15u,
    ADC_INJECTED_STATUS_FLAGS = ADC_STS_JENDC |
                                ADC_STS_JSTR |
                                ADC_STS_JENDCA,
    ADC_CONVERSION_CYCLES_X2 = 25u,
    ADC_CURRENT_SAMPLE_CYCLES_X2 = 15u,
    ADC_VBUS_SAMPLE_CYCLES_X2 = 111u,
    ADC_CURRENT_AND_VBUS_CYCLES_X2 =
        (2u * (ADC_CURRENT_SAMPLE_CYCLES_X2 +
               ADC_CONVERSION_CYCLES_X2)) +
        ADC_VBUS_SAMPLE_CYCLES_X2 + ADC_CONVERSION_CYCLES_X2,
    ADC_POST_TRIGGER_BUDGET_CYCLES_X2 =
        2u * (ADC1_MAX_CLOCK_HZ /
              ADC1_SYNCHRONOUS_CURRENT_FREQUENCY_HZ) *
        (1000u - TIM2_CURRENT_TRIGGER_PHASE_PERMILLE) / 1000u,
    DMA_REQUEST_ADC = 0u,
    DMA_CURRENT_CONFIGURATION = DMA_CHCFG1_TXCIE |
                                DMA_CHCFG1_ERRIE |
                                DMA_CHCFG1_CIRC |
                                DMA_CHCFG1_MINC |
                                DMA_CHCFG1_PSIZE_0 |
                                DMA_CHCFG1_MSIZE_0 |
                                DMA_CHCFG1_PRIOLVL_1,
    DMA_CHANNEL1_ALL_INTERRUPT_FLAGS =
        DMA_INTCLR_CGLBF1 | DMA_INTCLR_CTXCF1 |
        DMA_INTCLR_CHTXF1 | DMA_INTCLR_CERRF1,
    ADC_SNAPSHOT_RETRY_LIMIT = 8u
};

_Static_assert(DMA_CHANNEL_ADC_CURRENT == 1u,
               "ADC current acquisition owns DMA channel 1");
_Static_assert((ADC_CURRENT_DMA_BUFFER_LENGTH %
                ADC_CURRENT_SEQUENCE_LENGTH) == 0u,
               "DMA buffer must contain complete ADC sequences");
_Static_assert((ADC1_MAX_CLOCK_HZ %
                ADC1_SYNCHRONOUS_CURRENT_FREQUENCY_HZ) == 0u,
               "ADC timing budget requires an integral carrier period");
_Static_assert(ADC_CURRENT_AND_VBUS_CYCLES_X2 <
                   ADC_POST_TRIGGER_BUDGET_CYCLES_X2,
               "regular current plus injected VBUS exceeds carrier budget");
typedef struct
{
    uint8_t divisor;
    uint8_t register_value;
} adc_clock_divider_t;

static const adc_clock_divider_t ADC_CLOCK_DIVIDERS[] = {
    {1u, 0u},
    {2u, 1u},
    {4u, 2u},
    {6u, 3u},
    {8u, 4u},
    {10u, 5u},
    {12u, 6u},
    {16u, 7u},
    {32u, 8u},
};

static bool s_adc1_initialized;
static bool s_synchronous_current_started;
static uint32_t s_adc_sampling_clock_hz;
static uint32_t s_capture_index;
static volatile uint32_t s_current_snapshot_sequence;
static uint32_t s_last_read_snapshot_sequence;
static uint32_t s_vbus_sample_count;
static volatile adc1_status_t s_synchronous_status =
    ADC1_STATUS_NOT_READY;
static volatile adc1_current_snapshot_t s_latest_current_snapshot;
static adc1_current_event_handler_t s_current_event_handler;
static void* s_current_event_context;
static volatile uint16_t
    s_current_dma_buffer[ADC_CURRENT_DMA_BUFFER_LENGTH];

static bool select_adc_clock(uint32_t hclk_hz,
                             uint32_t* register_value,
                             uint32_t* adc_clock_hz)
{
    size_t index;

    if ((register_value == NULL) ||
        (adc_clock_hz == NULL) ||
        (hclk_hz < 1000000u) ||
        (hclk_hz > (ADC1_MAX_CLOCK_HZ * 32u)))
    {
        return false;
    }

    for (index = 0u;
         index < (sizeof(ADC_CLOCK_DIVIDERS) /
                  sizeof(ADC_CLOCK_DIVIDERS[0]));
         ++index)
    {
        if (hclk_hz <= (ADC1_MAX_CLOCK_HZ *
                        ADC_CLOCK_DIVIDERS[index].divisor))
        {
            *register_value = ADC_CLOCK_DIVIDERS[index].register_value;
            *adc_clock_hz = hclk_hz / ADC_CLOCK_DIVIDERS[index].divisor;
            return true;
        }
    }

    return false;
}

static bool wait_for_mask(volatile uint32_t* register_address,
                          uint32_t mask,
                          bool expected_set)
{
    uint32_t remaining = ADC_POLL_BUDGET;

    while (remaining != 0u)
    {
        const bool is_set = ((*register_address & mask) == mask);
        if (is_set == expected_set)
        {
            return true;
        }
        --remaining;
    }

    return false;
}

static void rollback_adc_init(bool hsi_was_enabled)
{
    ADC->CTRL2 &= ~ADC_CTRL2_ON;
    RCC->AHBPRST |= RCC_AHBRST_ADCRST;
    RCC->AHBPRST &= ~RCC_AHBRST_ADCRST;
    RCC->AHBPCLKEN &= ~RCC_AHBPCLKEN_ADCEN;

    if (!hsi_was_enabled)
    {
        RCC->CTRL &= ~RCC_CTRL_HSIEN;
    }
}

static adc1_status_t convert_channel(uint8_t channel, uint16_t* output)
{
    uint32_t remaining = ADC_POLL_BUDGET;
    uint32_t raw;

    if (output == NULL)
    {
        return ADC1_STATUS_INVALID_ARGUMENT;
    }
    if ((ADC->STS & ADC_STS_STR) != 0u)
    {
        return ADC1_STATUS_BUSY;
    }

    ADC->RSEQ3 = (ADC->RSEQ3 & ~ADC_RSEQ3_SEQ1) |
                 ((uint32_t)channel & ADC_RSEQ3_SEQ1);
    ADC->STS = (~((uint32_t)ADC_STS_AWDG |
                  ADC_STS_ENDC |
                  ADC_STS_STR |
                  ADC_STS_ENDCA)) &
               ADC_STATUS_WRITABLE_MASK;
    ADC->CTRL2 |= ADC_SOFTWARE_START;

    while (remaining != 0u)
    {
        const uint32_t status = ADC->STS;

        if ((status & ADC_STS_AWDG) != 0u)
        {
            return ADC1_STATUS_DATA_OUT_OF_RANGE;
        }
        if ((status & ADC_STS_ENDC) != 0u)
        {
            raw = ADC->DAT & ADC_DAT_DAT;
            ADC->STS = (~((uint32_t)ADC_STS_ENDC |
                          ADC_STS_STR |
                          ADC_STS_ENDCA)) &
                       ADC_STATUS_WRITABLE_MASK;
            if (raw > ADC_SAMPLE_RAW_MAX)
            {
                return ADC1_STATUS_DATA_OUT_OF_RANGE;
            }
            *output = (uint16_t)raw;
            return ADC1_STATUS_OK;
        }
        --remaining;
    }

    return ADC1_STATUS_CONVERSION_TIMEOUT;
}

adc1_status_t adc1_init_passive(uint32_t hclk_hz)
{
    uint32_t adc_clock_setting;
    uint32_t adc_sampling_clock_hz;
    uint32_t clock_configuration;
    volatile uint32_t* const adc_ldo_control =
        (volatile uint32_t*)(uintptr_t)(ADC_BASE + ADC_LDO_CONTROL_OFFSET);
    const bool hsi_was_enabled =
        (RCC->CTRL & RCC_CTRL_HSIEN) != 0u;

    if (!select_adc_clock(hclk_hz,
                          &adc_clock_setting,
                          &adc_sampling_clock_hz))
    {
        return ADC1_STATUS_UNSUPPORTED_CLOCK;
    }

    s_adc1_initialized = false;
    s_synchronous_current_started = false;
    s_adc_sampling_clock_hz = 0u;
    s_capture_index = 0u;
    s_current_snapshot_sequence = 0u;
    s_last_read_snapshot_sequence = 0u;
    s_vbus_sample_count = 0u;
    s_synchronous_status = ADC1_STATUS_NOT_READY;
    s_current_event_handler = NULL;
    s_current_event_context = NULL;
    NVIC_DisableIRQ(DMA_Channel1_IRQn);

    RCC->CTRL |= RCC_CTRL_HSIEN;
    if (!wait_for_mask(&RCC->CTRL, RCC_CTRL_HSIRDF, true))
    {
        if (!hsi_was_enabled)
        {
            RCC->CTRL &= ~RCC_CTRL_HSIEN;
        }
        return ADC1_STATUS_CLOCK_TIMEOUT;
    }

    clock_configuration = RCC->CFG2;
    clock_configuration &= ~(RCC_CFG2_ADCHPRES |
                             RCC_CFG2_ADCPLLPRES |
                             RCC_CFG2_ADC1MPRES |
                             RCC_CFG2_ADC1MSEL);
    clock_configuration |= adc_clock_setting |
                           RCC_CFG2_ADC1MPRES_DIV16 |
                           RCC_CFG2_ADC1MSEL_HSI;
    RCC->CFG2 = clock_configuration;

    RCC->APB2PCLKEN |= RCC_APB2PCLKEN_IOPAEN;
    RCC->AHBPCLKEN |= RCC_AHBPCLKEN_ADCEN;
    __DSB();

    RCC->AHBPRST |= RCC_AHBRST_ADCRST;
    RCC->AHBPRST &= ~RCC_AHBRST_ADCRST;

    GPIOA->PUPD &= ~((uint32_t)ADC_GPIO_MODE_MASK);
    GPIOA->PMODE = (GPIOA->PMODE & ~((uint32_t)ADC_GPIO_MODE_MASK)) |
                   (uint32_t)ADC_GPIO_MODE_ANALOG;

    /* Required by Nations' V1.2.2 ADC driver but not named in the TRM. */
    *adc_ldo_control |= ADC_LDO_ENABLE_VALUE;

    ADC->CTRL1 = 0u;
    ADC->CTRL2 = ADC_SOFTWARE_TRIGGER_SELECT;
    ADC->CTRL3 = ADC_CTRL3_RESOLUTION_12BIT;
    ADC->RSEQ1 &= ~ADC_RSEQ1_LEN;
    ADC->RSEQ3 = ADC1_CURRENT_B_CHANNEL;
    ADC->SAMPT2 = (ADC->SAMPT2 &
                   ~(ADC_SAMPT2_SAMP2 |
                     ADC_SAMPT2_SAMP3 |
                     ADC_SAMPT2_SAMP4)) |
                  ((uint32_t)ADC_SAMPLE_TIME_28CYCLES5 << (2u * 3u)) |
                  ((uint32_t)ADC_SAMPLE_TIME_28CYCLES5 << (3u * 3u)) |
                  ((uint32_t)ADC_SAMPLE_TIME_55CYCLES5 << (4u * 3u));
    ADC->STS = 0u;

    ADC->CTRL2 |= ADC_CTRL2_ON;
    if (!wait_for_mask(&ADC->CTRL3, ADC_CTRL3_READY, true))
    {
        rollback_adc_init(hsi_was_enabled);
        return ADC1_STATUS_POWER_TIMEOUT;
    }

    ADC->CTRL2 |= ADC_CTRL2_ENCAL;
    if (!wait_for_mask(&ADC->CTRL2, ADC_CTRL2_ENCAL, false))
    {
        rollback_adc_init(hsi_was_enabled);
        return ADC1_STATUS_CALIBRATION_TIMEOUT;
    }

    s_adc1_initialized = true;
    s_adc_sampling_clock_hz = adc_sampling_clock_hz;
    return ADC1_STATUS_OK;
}

adc1_status_t adc1_read_passive(adc_sample_t* output)
{
    adc1_status_t result;
    uint16_t current_b_raw;
    uint16_t current_a_raw;
    uint16_t vbus_raw;
    uint32_t capture_index;

    if (output == NULL)
    {
        return ADC1_STATUS_INVALID_ARGUMENT;
    }
    if (!s_adc1_initialized)
    {
        return ADC1_STATUS_NOT_READY;
    }
    if (s_synchronous_current_started)
    {
        return ADC1_STATUS_BUSY;
    }

    result = convert_channel(ADC1_CURRENT_B_CHANNEL, &current_b_raw);
    if (result != ADC1_STATUS_OK)
    {
        return result;
    }
    result = convert_channel(ADC1_CURRENT_A_CHANNEL, &current_a_raw);
    if (result != ADC1_STATUS_OK)
    {
        return result;
    }
    result = convert_channel(ADC1_VBUS_CHANNEL, &vbus_raw);
    if (result != ADC1_STATUS_OK)
    {
        return result;
    }

    capture_index = s_capture_index + 1u;
    if (!adc_sample_build(output,
                          current_b_raw,
                          current_a_raw,
                          vbus_raw,
                          capture_index))
    {
        return ADC1_STATUS_DATA_OUT_OF_RANGE;
    }

    s_capture_index = capture_index;
    return ADC1_STATUS_OK;
}

adc1_status_t adc1_start_pwm_synchronized_current(void)
{
    if (!s_adc1_initialized)
    {
        return ADC1_STATUS_NOT_READY;
    }
    if (s_synchronous_current_started)
    {
        return ADC1_STATUS_OK;
    }
    if ((ADC->STS & ADC_STS_STR) != 0u)
    {
        return ADC1_STATUS_BUSY;
    }
    if (((uint64_t)ADC_CURRENT_AND_VBUS_CYCLES_X2 *
         ADC1_SYNCHRONOUS_CURRENT_FREQUENCY_HZ * 1000u) >=
        ((uint64_t)2u * s_adc_sampling_clock_hz *
         (1000u - TIM2_CURRENT_TRIGGER_PHASE_PERMILLE)))
    {
        return ADC1_STATUS_UNSUPPORTED_CLOCK;
    }

    RCC->AHBPCLKEN |= RCC_AHBPCLKEN_DMAEN;
    __DSB();

    if ((DMA_CH1->CHCFG & DMA_CHCFG1_CHEN) != 0u)
    {
        return ADC1_STATUS_BUSY;
    }

    /*
     * Configure and arm the complete current-acquisition path before TIM3 is
     * started. This preserves the manufacturer-proven DMA initialization
     * order while restoring the target two-rank external-triggered sequence.
     */
    ADC->CTRL2 = 0u;
    DMA_CH1->CHCFG = 0u;
    DMA_CH1->TXNUM = 0u;
    DMA_CH1->PADDR = 0u;
    DMA_CH1->MADDR = 0u;
    DMA->INTCLR = DMA_CHANNEL1_ALL_INTERRUPT_FLAGS;
    s_current_snapshot_sequence = 0u;
    s_last_read_snapshot_sequence = 0u;
    s_vbus_sample_count = 0u;
    s_synchronous_status = ADC1_STATUS_NO_SAMPLE;
    DMA_CH1->PADDR = (uint32_t)(uintptr_t)&ADC->DAT;
    DMA_CH1->MADDR =
        (uint32_t)(uintptr_t)&s_current_dma_buffer[0];
    DMA_CH1->TXNUM = ADC_CURRENT_DMA_BUFFER_LENGTH;
    DMA_CH1->CHSEL = DMA_REQUEST_ADC;
    DMA_CH1->CHCFG = DMA_CURRENT_CONFIGURATION;

    NVIC_DisableIRQ(DMA_Channel1_IRQn);
    NVIC_ClearPendingIRQ(DMA_Channel1_IRQn);
    NVIC_SetPriority(DMA_Channel1_IRQn,
                     INTERRUPT_PRIORITY_FAST_CURRENT);
    NVIC_EnableIRQ(DMA_Channel1_IRQn);
    DMA_CH1->CHCFG |= DMA_CHCFG1_CHEN;

    /* One slow PA3 injected conversion follows each regular current pair.
       A one-rank injected sequence occupies JSEQ4 on this ADC. Automatic
       injection leaves regular DMA completion as the current-loop event. */
    ADC->CTRL1 = ADC_CTRL1_SCANMD | ADC_CTRL1_AUTOJC;
    ADC->CTRL2 = ADC_SOFTWARE_TRIGGER_SELECT;
    ADC->RSEQ1 = (ADC->RSEQ1 & ~ADC_RSEQ1_LEN) |
                 ((uint32_t)ADC_CURRENT_SEQUENCE_LENGTH_ENCODING << 20u);
    ADC->RSEQ3 =
        ((uint32_t)ADC1_CURRENT_B_CHANNEL & ADC_RSEQ3_SEQ1) |
        (((uint32_t)ADC1_CURRENT_A_CHANNEL << 5u) & ADC_RSEQ3_SEQ2);
    ADC->JSEQ =
        ((uint32_t)ADC1_VBUS_CHANNEL << ADC_INJECTED_VBUS_RANK_SHIFT) &
        ADC_JSEQ_JSEQ4;
    ADC->SAMPT2 = (ADC->SAMPT2 &
                   ~(ADC_SAMPT2_SAMP2 |
                     ADC_SAMPT2_SAMP3 |
                     ADC_SAMPT2_SAMP4)) |
                  ((uint32_t)ADC_SAMPLE_TIME_7CYCLES5 << (2u * 3u)) |
                  ((uint32_t)ADC_SAMPLE_TIME_7CYCLES5 << (3u * 3u)) |
                  ((uint32_t)ADC_SAMPLE_TIME_55CYCLES5 << (4u * 3u));
    ADC->STS = 0u;

    ADC->CTRL2 |= ADC_CTRL2_ON;
    if (!wait_for_mask(&ADC->CTRL3, ADC_CTRL3_READY, true))
    {
        return ADC1_STATUS_POWER_TIMEOUT;
    }
    ADC->CTRL2 |= ADC_CTRL2_ENCAL;
    if (!wait_for_mask(&ADC->CTRL2, ADC_CTRL2_ENCAL, false))
    {
        return ADC1_STATUS_CALIBRATION_TIMEOUT;
    }

    ADC->CTRL2 |= ADC_CTRL2_ENDMA | ADC_CTRL2_EXTRTRIG;
    __DSB();

    s_synchronous_current_started =
        ((DMA_CH1->CHCFG & DMA_CHCFG1_CHEN) != 0u) &&
        (DMA_CH1->CHSEL == DMA_REQUEST_ADC) &&
        ((ADC->CTRL2 & (ADC_CTRL2_ENDMA | ADC_CTRL2_EXTRTRIG |
                        ADC_CTRL2_EXTRSEL)) ==
         (ADC_CTRL2_ENDMA | ADC_CTRL2_EXTRTRIG |
           ADC_SOFTWARE_TRIGGER_SELECT));
    if (!s_synchronous_current_started)
    {
        NVIC_DisableIRQ(DMA_Channel1_IRQn);
        s_synchronous_status = ADC1_STATUS_NOT_READY;
        return ADC1_STATUS_NOT_READY;
    }

    return ADC1_STATUS_OK;
}

adc1_status_t adc1_read_synchronized_current(
    adc1_current_snapshot_t* output)
{
    uint32_t attempt;
    const adc1_status_t status = s_synchronous_status;

    if (output == NULL)
    {
        return ADC1_STATUS_INVALID_ARGUMENT;
    }
    if ((status != ADC1_STATUS_OK) &&
        (status != ADC1_STATUS_NO_SAMPLE))
    {
        return status;
    }
    if (!s_synchronous_current_started)
    {
        return ADC1_STATUS_NOT_READY;
    }
    for (attempt = 0u; attempt < ADC_SNAPSHOT_RETRY_LIMIT; ++attempt)
    {
        const uint32_t sequence_before = s_current_snapshot_sequence;
        uint16_t current_b_raw;
        uint16_t current_a_raw;

        if ((sequence_before == 0u) ||
            ((sequence_before & 1u) != 0u) ||
            (sequence_before == s_last_read_snapshot_sequence))
        {
            return ADC1_STATUS_NO_SAMPLE;
        }

        __DMB();
        current_b_raw = s_latest_current_snapshot.current_b_raw;
        current_a_raw = s_latest_current_snapshot.current_a_raw;
        __DMB();
        if (sequence_before == s_current_snapshot_sequence)
        {
            if ((current_b_raw > ADC_SAMPLE_RAW_MAX) ||
                (current_a_raw > ADC_SAMPLE_RAW_MAX))
            {
                return ADC1_STATUS_DATA_OUT_OF_RANGE;
            }

            output->current_a_raw = current_a_raw;
            output->current_b_raw = current_b_raw;
            s_last_read_snapshot_sequence = sequence_before;
            return ADC1_STATUS_OK;
        }
    }

    return ADC1_STATUS_BUSY;
}

adc1_status_t adc1_read_synchronized_vbus(
    adc1_vbus_snapshot_t* output)
{
    uint32_t raw;

    if (output == NULL)
    {
        return ADC1_STATUS_INVALID_ARGUMENT;
    }
    if (!s_synchronous_current_started)
    {
        return ADC1_STATUS_NOT_READY;
    }
    if ((ADC->STS & ADC_STS_JENDC) == 0u)
    {
        return ADC1_STATUS_NO_SAMPLE;
    }

    raw = ADC->JDAT1 & ADC_JDAT1_JDAT;
    ADC->STS = (~((uint32_t)ADC_INJECTED_STATUS_FLAGS)) &
               ADC_STATUS_WRITABLE_MASK;
    if (raw > ADC_SAMPLE_RAW_MAX)
    {
        return ADC1_STATUS_DATA_OUT_OF_RANGE;
    }

    ++s_vbus_sample_count;
    output->vbus_raw = (uint16_t)raw;
    output->sample_count = s_vbus_sample_count;
    return ADC1_STATUS_OK;
}

bool adc1_set_current_event_handler(
    adc1_current_event_handler_t handler,
    void* context)
{
    if (!s_synchronous_current_started)
    {
        return false;
    }

    /* Publish context before the handler. An interrupt between the NULL store
       and final publication safely skips one callback. */
    s_current_event_handler = NULL;
    __DMB();
    s_current_event_context = context;
    __DMB();
    s_current_event_handler = handler;
    __DMB();
    return true;
}

bool adc1_trigger_synchronized_current_from_isr(void)
{
    if (!s_synchronous_current_started ||
        ((ADC->STS & ADC_STS_STR) != 0u))
    {
        return false;
    }

    ADC->CTRL2 |= ADC_CTRL2_SWSTRRCH;
    return true;
}

void DMA_Channel1_IRQHandler(void)
{
    const uint32_t flags = DMA->INTSTS;
    adc1_current_event_handler_t handler;
    void* context;

    if ((flags & DMA_INTSTS_ERRF1) != 0u)
    {
        DMA->INTCLR = DMA_CHANNEL1_ALL_INTERRUPT_FLAGS;
        DMA_CH1->CHCFG &= ~((uint32_t)DMA_CHCFG1_CHEN);
        s_synchronous_status = ADC1_STATUS_DMA_ERROR;
        s_synchronous_current_started = false;
        handler = s_current_event_handler;
        context = s_current_event_context;
        if (handler != NULL)
        {
            handler(ADC1_STATUS_DMA_ERROR, NULL, context);
        }
        return;
    }

    if ((flags & DMA_INTSTS_TXCF1) != 0u)
    {
        adc1_current_snapshot_t snapshot;

        DMA->INTCLR = DMA_INTCLR_CTXCF1 | DMA_INTCLR_CGLBF1;
        snapshot.current_b_raw = s_current_dma_buffer[0];
        snapshot.current_a_raw = s_current_dma_buffer[1];
        /* STR records that a regular conversion started; it is not a live
           busy bit. The N32L40x manual requires software to clear it. Clear
           the completed sequence flags here so the next timed software
           trigger is accepted instead of permanently rejecting every
           sequence after the first one. */
        ADC->STS = (~((uint32_t)ADC_STS_ENDC |
                      ADC_STS_STR |
                      ADC_STS_ENDCA)) &
                   ADC_STATUS_WRITABLE_MASK;
        if ((snapshot.current_b_raw > ADC_SAMPLE_RAW_MAX) ||
            (snapshot.current_a_raw > ADC_SAMPLE_RAW_MAX))
        {
            DMA_CH1->CHCFG &= ~((uint32_t)DMA_CHCFG1_CHEN);
            s_synchronous_status = ADC1_STATUS_DATA_OUT_OF_RANGE;
            s_synchronous_current_started = false;
            handler = s_current_event_handler;
            context = s_current_event_context;
            if (handler != NULL)
            {
                handler(ADC1_STATUS_DATA_OUT_OF_RANGE, NULL, context);
            }
            return;
        }

        ++s_current_snapshot_sequence;
        __DMB();
        s_latest_current_snapshot = snapshot;
        __DMB();
        ++s_current_snapshot_sequence;
        s_synchronous_status = ADC1_STATUS_OK;

        handler = s_current_event_handler;
        context = s_current_event_context;
        if (handler != NULL)
        {
            handler(ADC1_STATUS_OK, &snapshot, context);
        }
    }
}
