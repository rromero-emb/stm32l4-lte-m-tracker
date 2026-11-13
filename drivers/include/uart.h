#ifndef UART_H
#define UART_H

#include <stddef.h>
#include <stdint.h>

#include "ringbuf.h"

// USART driver, RX via DMA in circular mode: new data is collected on IDLE line, half-transfer
// and transfer-complete events into a ring buffer. TX: polled. Two ports on the NUCLEO-L476RG:
//   uart_console: USART2, PA2/PA3 (AF7), DMA1 channel 6: the ST-LINK virtual COM port
//   uart_modem:   USART1, PA9/PA10 (AF7), DMA1 channel 5: the BG95 (Arduino D8 / D2)
// USART1/2 stop in Stop 2: whoever needs reception holds a power-management lock (pm.h).

#define UART_RX_DMA_SIZE 64U
#define UART_RX_RING_SIZE 512U

// USARTDIV for oversampling by 16 (RM0351 40.5.4), rounded to nearest.
static inline uint32_t uart_brr(uint32_t pclk_hz, uint32_t baud)
{
    return (pclk_hz + (baud / 2U)) / baud;
}

typedef void (*uart_rx_cb_t)(void *ctx);  // called from the interrupt when bytes arrived

typedef struct uart_hw uart_hw_t;

typedef struct {
    const uart_hw_t *hw;
    uint8_t dma_buf[UART_RX_DMA_SIZE];
    uint8_t ring_storage[UART_RX_RING_SIZE];
    ringbuf_t ring;
    size_t last;
    volatile uint32_t dropped;
    volatile uint32_t overruns;
    uart_rx_cb_t on_rx;
    void *ctx;
} uart_t;

extern uart_t uart_console;
extern uart_t uart_modem;

void uart_init(uart_t *u, uint32_t pclk_hz, uint32_t baud, uart_rx_cb_t on_rx, void *ctx);
void uart_write(uart_t *u, const char *data, size_t len);
void uart_puts(uart_t *u, const char *s);
size_t uart_read(uart_t *u, uint8_t *data, size_t len);
// Bytes lost because the ring buffer was full, and USART overrun events.
uint32_t uart_rx_dropped(const uart_t *u);
uint32_t uart_rx_overruns(const uart_t *u);

#endif
