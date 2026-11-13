#include "uart.h"

#include <string.h>

#include "gpio.h"
#include "stm32l4xx.h"

struct uart_hw {
    USART_TypeDef *usart;
    DMA_Channel_TypeDef *dma;
    uint8_t dma_ch;   // 1..7, for the DMA1 ISR/IFCR/CSELR bit positions
    uint8_t dma_req;  // DMA1_CSELR request number (RM0351 table 41)
    GPIO_TypeDef *port;
    uint8_t tx_pin;
    uint8_t rx_pin;
    uint8_t af;
    IRQn_Type usart_irq;
    IRQn_Type dma_irq;
    volatile uint32_t *enr;  // RCC enable register and bit
    uint32_t enr_bit;
};

static const uart_hw_t console_hw = {
    USART2,         DMA1_Channel6,        6U, 2U, GPIOA, 2U, 3U, 7U, USART2_IRQn, DMA1_Channel6_IRQn,
    &RCC->APB1ENR1, RCC_APB1ENR1_USART2EN};
static const uart_hw_t modem_hw = {
    USART1,        DMA1_Channel5,       5U, 2U, GPIOA, 9U, 10U, 7U, USART1_IRQn, DMA1_Channel5_IRQn,
    &RCC->APB2ENR, RCC_APB2ENR_USART1EN};

uart_t uart_console = {.hw = &console_hw};
uart_t uart_modem = {.hw = &modem_hw};

#define DMA_SHIFT(u) (4U * ((uint32_t)(u)->hw->dma_ch - 1U))

static void rx_collect(uart_t *u)
{
    size_t pos = UART_RX_DMA_SIZE - u->hw->dma->CNDTR;
    if (pos >= UART_RX_DMA_SIZE) {
        pos = 0U;
    }
    size_t before = u->last;
    size_t expected = (pos >= before) ? (pos - before) : (UART_RX_DMA_SIZE - before + pos);
    size_t copied = dma_circ_consume(u->dma_buf, UART_RX_DMA_SIZE, &u->last, pos, &u->ring);
    u->dropped += (uint32_t)(expected - copied);
    if ((copied > 0U) && (u->on_rx != NULL)) {
        u->on_rx(u->ctx);
    }
}

void uart_init(uart_t *u, uint32_t pclk_hz, uint32_t baud, uart_rx_cb_t on_rx, void *ctx)
{
    const uart_hw_t *hw = u->hw;
    (void)ringbuf_init(&u->ring, u->ring_storage, sizeof u->ring_storage);
    u->last = 0U;
    u->on_rx = on_rx;
    u->ctx = ctx;

    gpio_clock_enable(hw->port);
    gpio_set_af(hw->port, hw->tx_pin, hw->af);
    gpio_set_af(hw->port, hw->rx_pin, hw->af);
    gpio_set_pull(hw->port, hw->rx_pin, GPIO_PULL_UP);  // idle-high line if nothing is connected

    *hw->enr |= hw->enr_bit;
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA1EN;
    (void)RCC->AHB1ENR;

    // DMA channel <- USART RX, peripheral-to-memory, 8-bit, memory increment, circular.
    uint32_t sh = DMA_SHIFT(u);
    hw->dma->CCR = 0U;
    DMA1_CSELR->CSELR = (DMA1_CSELR->CSELR & ~(0xFUL << sh)) | ((uint32_t)hw->dma_req << sh);
    hw->dma->CPAR = (uint32_t)&hw->usart->RDR;
    hw->dma->CMAR = (uint32_t)u->dma_buf;
    hw->dma->CNDTR = UART_RX_DMA_SIZE;
    DMA1->IFCR = 0xFUL << sh;
    hw->dma->CCR = DMA_CCR_MINC | DMA_CCR_CIRC | DMA_CCR_HTIE | DMA_CCR_TCIE | DMA_CCR_EN;

    hw->usart->CR1 = 0U;
    hw->usart->BRR = uart_brr(pclk_hz, baud);
    hw->usart->CR3 = USART_CR3_DMAR;
    hw->usart->ICR = USART_ICR_IDLECF | USART_ICR_ORECF;
    hw->usart->CR1 = USART_CR1_IDLEIE | USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;

    // 5 = configMAX_SYSCALL_INTERRUPT_PRIORITY: the callback may use FreeRTOS "FromISR" calls.
    NVIC_SetPriority(hw->dma_irq, 5U);
    NVIC_SetPriority(hw->usart_irq, 5U);
    NVIC_EnableIRQ(hw->dma_irq);
    NVIC_EnableIRQ(hw->usart_irq);
}

static void usart_irq(uart_t *u)
{
    USART_TypeDef *usart = u->hw->usart;
    uint32_t isr = usart->ISR;
    if ((isr & USART_ISR_ORE) != 0U) {
        usart->ICR = USART_ICR_ORECF;
        u->overruns++;
    }
    if ((isr & USART_ISR_IDLE) != 0U) {
        usart->ICR = USART_ICR_IDLECF;
        rx_collect(u);
    }
}

static void dma_irq(uart_t *u)
{
    uint32_t sh = DMA_SHIFT(u);
    uint32_t ht_tc = (DMA_ISR_TCIF1 | DMA_ISR_HTIF1) << sh;
    if ((DMA1->ISR & ht_tc) != 0U) {
        DMA1->IFCR = ht_tc;
        rx_collect(u);
    }
}

void USART2_IRQHandler(void)
{
    usart_irq(&uart_console);
}

void DMA1_Channel6_IRQHandler(void)
{
    dma_irq(&uart_console);
}

void USART1_IRQHandler(void)
{
    usart_irq(&uart_modem);
}

void DMA1_Channel5_IRQHandler(void)
{
    dma_irq(&uart_modem);
}

void uart_write(uart_t *u, const char *data, size_t len)
{
    USART_TypeDef *usart = u->hw->usart;
    for (size_t i = 0; i < len; i++) {
        while ((usart->ISR & USART_ISR_TXE) == 0U) {
        }
        usart->TDR = (uint8_t)data[i];
    }
    // Wait for the last stop bit: entering Stop 2 earlier would cut the frame.
    while ((usart->ISR & USART_ISR_TC) == 0U) {
    }
}

void uart_puts(uart_t *u, const char *s)
{
    uart_write(u, s, strlen(s));
}

size_t uart_read(uart_t *u, uint8_t *data, size_t len)
{
    return ringbuf_read(&u->ring, data, len);
}

uint32_t uart_rx_dropped(const uart_t *u)
{
    return u->dropped;
}

uint32_t uart_rx_overruns(const uart_t *u)
{
    return u->overruns;
}
