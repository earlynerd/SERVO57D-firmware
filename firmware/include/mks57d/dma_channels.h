#ifndef MKS57D_DMA_CHANNELS_H
#define MKS57D_DMA_CHANNELS_H

/*
 * Compile-time ownership for the provisional RUN DMA budget. The N32L406
 * request selector can route any supported request to any of the eight
 * channels, so channel numbers are an architectural resource rather than a
 * fixed peripheral mapping.
 */
enum
{
    DMA_CHANNEL_ADC_CURRENT = 1u,
    DMA_CHANNEL_ENCODER_RX = 2u,
    DMA_CHANNEL_ENCODER_TX = 3u,
    DMA_CHANNEL_USART1_RX = 4u,
    DMA_CHANNEL_USART1_TX = 5u,
    DMA_CHANNEL_TIM3_BURST = 6u,
    DMA_CHANNEL_SPARE_1 = 7u,
    DMA_CHANNEL_SPARE_2 = 8u
};

#endif
