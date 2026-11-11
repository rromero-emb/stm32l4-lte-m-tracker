#include "uart.h"

#include <string.h>

#include "gpio.h"
#include "ringbuf.h"
#include "stm32l4xx.h"

#define UART_TX_PIN 2U
#define UART_RX_PIN 3U
#define UART_AF 7U
#define DMA_REQ_USART2 2U  // DMA1_CSELR request number for USART2 on channels 6/7 (RM0351 table 41)

static uint8_t rx_dma_buf[UART_RX_DMA_SIZE];
static uint8_t rx_ring_storage[UART_RX_RING_SIZE];
static ringbuf_t rx_ring;
static size_t rx_last;
static volatile uint32_t rx_dropped;
static volatile uint32_t rx_overruns;

static void rx_collect(void)
{
    size_t pos = UART_RX_DMA_SIZE - DMA1_Channel6->CNDTR;
    if (pos >= UART_RX_DMA_SIZE) {
        pos = 0U;
    }
    size_t before = rx_last;
    size_t expected = (pos >= before) ? (pos - before) : (UART_RX_DMA_SIZE - before + pos);
    size_t copied = dma_circ_consume(rx_dma_buf, UART_RX_DMA_SIZE, &rx_last, pos, &rx_ring);
    rx_dropped += (uint32_t)(expected - copied);
}

void uart_init(uint32_t pclk_hz, uint32_t baud)
{
    (void)ringbuf_init(&rx_ring, rx_ring_storage, sizeof rx_ring_storage);
    rx_last = 0U;

    gpio_clock_enable(GPIOA);
    gpio_set_af(GPIOA, UART_TX_PIN, UART_AF);
    gpio_set_af(GPIOA, UART_RX_PIN, UART_AF);
    gpio_set_pull(GPIOA, UART_RX_PIN, GPIO_PULL_UP);  // idle-high line if nothing is connected

    RCC->APB1ENR1 |= RCC_APB1ENR1_USART2EN;
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA1EN;
    (void)RCC->AHB1ENR;

    // DMA1 channel 6 <- USART2_RX, peripheral-to-memory, 8-bit, memory increment, circular.
    DMA1_Channel6->CCR = 0U;
    DMA1_CSELR->CSELR = (DMA1_CSELR->CSELR & ~DMA_CSELR_C6S) | (DMA_REQ_USART2 << DMA_CSELR_C6S_Pos);
    DMA1_Channel6->CPAR = (uint32_t)&USART2->RDR;
    DMA1_Channel6->CMAR = (uint32_t)rx_dma_buf;
    DMA1_Channel6->CNDTR = UART_RX_DMA_SIZE;
    DMA1->IFCR = DMA_IFCR_CGIF6;
    DMA1_Channel6->CCR = DMA_CCR_MINC | DMA_CCR_CIRC | DMA_CCR_HTIE | DMA_CCR_TCIE | DMA_CCR_EN;

    USART2->CR1 = 0U;
    USART2->BRR = uart_brr(pclk_hz, baud);
    USART2->CR3 = USART_CR3_DMAR;
    USART2->ICR = USART_ICR_IDLECF | USART_ICR_ORECF;
    USART2->CR1 = USART_CR1_IDLEIE | USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;

    NVIC_SetPriority(DMA1_Channel6_IRQn, 5U);
    NVIC_SetPriority(USART2_IRQn, 5U);
    NVIC_EnableIRQ(DMA1_Channel6_IRQn);
    NVIC_EnableIRQ(USART2_IRQn);
}

void USART2_IRQHandler(void)
{
    uint32_t isr = USART2->ISR;
    if ((isr & USART_ISR_ORE) != 0U) {
        USART2->ICR = USART_ICR_ORECF;
        rx_overruns++;
    }
    if ((isr & USART_ISR_IDLE) != 0U) {
        USART2->ICR = USART_ICR_IDLECF;
        rx_collect();
    }
}

void DMA1_Channel6_IRQHandler(void)
{
    uint32_t isr = DMA1->ISR;
    if ((isr & (DMA_ISR_HTIF6 | DMA_ISR_TCIF6)) != 0U) {
        DMA1->IFCR = DMA_IFCR_CHTIF6 | DMA_IFCR_CTCIF6;
        rx_collect();
    }
}

void uart_write(const char *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        while ((USART2->ISR & USART_ISR_TXE) == 0U) {
        }
        USART2->TDR = (uint8_t)data[i];
    }
    while ((USART2->ISR & USART_ISR_TC) == 0U) {
    }
}

void uart_puts(const char *s)
{
    uart_write(s, strlen(s));
}

size_t uart_read(uint8_t *data, size_t len)
{
    return ringbuf_read(&rx_ring, data, len);
}

uint32_t uart_rx_dropped(void)
{
    return rx_dropped;
}

uint32_t uart_rx_overruns(void)
{
    return rx_overruns;
}
