#ifndef MKS57D_RS485_H
#define MKS57D_RS485_H

#include <stddef.h>
#include <stdint.h>

enum
{
    RS485_BAUD_RATE = 115200u,
    RS485_RX_DMA_BUFFER_SIZE = 256u,
    RS485_TX_MAX_FRAME_SIZE = 256u
};

typedef enum
{
    RS485_STATUS_OK = 0,
    RS485_STATUS_NOT_READY,
    RS485_STATUS_INVALID_ARGUMENT,
    RS485_STATUS_BUSY,
    RS485_STATUS_DMA_ERROR,
    RS485_STATUS_CONFIGURATION_ERROR
} rs485_status_t;

typedef struct
{
    uint32_t status;
    uint32_t rx_bytes;
    uint32_t rx_idle_events;
    uint32_t rx_error_count;
    uint32_t rx_overrun_count;
    uint32_t rx_dropped_bytes;
    uint32_t tx_bytes;
    uint32_t tx_frame_count;
    uint32_t tx_error_count;
    uint32_t tx_busy;
} rs485_stats_t;

/*
 * Active, receive-first USART1 transport for purchased-board bring-up:
 * PC13 low receives, PC13 high transmits, PA9 is USART1_TX, and PA10 is
 * USART1_RX. RX byte movement is continuous on DMA channel 4; TX frames use
 * DMA channel 5 and release PC13 only after the USART transmission-complete
 * event proves that the final stop bit has left the shifter.
 */
rs485_status_t rs485_init(uint32_t peripheral_clock_hz);

/* Foreground-only APIs. Framing and command validation belong above this layer. */
size_t rs485_read(uint8_t* destination, size_t capacity);
rs485_status_t rs485_write(const uint8_t* bytes, size_t length);
void rs485_get_stats(rs485_stats_t* stats);

#endif
