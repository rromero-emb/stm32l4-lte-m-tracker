#ifndef UART_H
#define UART_H

#include <stddef.h>
#include <stdint.h>

// USART2 on PA2 (TX) / PA3 (RX), AF7: the ST-LINK virtual COM port on Nucleo-64 boards.
// RX: DMA1 channel 6 (request 2) in circular mode; new data is collected on IDLE line,
// half-transfer and transfer-complete events into a ring buffer. TX: polled.

#define UART_RX_DMA_SIZE 64U
#define UART_RX_RING_SIZE 256U

// USARTDIV for oversampling by 16 (RM0351 40.5.4), rounded to nearest.
static inline uint32_t uart_brr(uint32_t pclk_hz, uint32_t baud)
{
    return (pclk_hz + (baud / 2U)) / baud;
}

void uart_init(uint32_t pclk_hz, uint32_t baud);
void uart_write(const char *data, size_t len);
void uart_puts(const char *s);
size_t uart_read(uint8_t *data, size_t len);
// Bytes lost because the ring buffer was full, and USART overrun events.
uint32_t uart_rx_dropped(void);
uint32_t uart_rx_overruns(void);

#endif
