// Session 1 demo for NUCLEO-L476RG: clocks, LED, UART console (RX via DMA), SPI2 loopback, fault capture.
//
// Wiring for the SPI loopback test: jumper PB15 (MOSI, CN10 pin 26) to PB14 (MISO, CN10 pin 28).
// Console: ST-LINK virtual COM port, 115200 8N1.
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "fault.h"
#include "fmt.h"
#include "gpio.h"
#include "payload.h"
#include "rcc.h"
#include "spi.h"
#include "stm32l4xx.h"
#include "systick.h"
#include "uart.h"

#define LED_PORT GPIOA
#define LED_PIN 5U  // LD2 (green) on Nucleo-64
#define SPI_PORT GPIOB
#define SPI_SCK_PIN 13U
#define SPI_MISO_PIN 14U
#define SPI_MOSI_PIN 15U
#define SPI_AF 5U

static spi_config_t spi_cfg = {
    .pclk_hz = RCC_PCLK1_HZ,  // SPI2 is on APB1
    .max_hz = 1000000U,
    .mode = 0U,
    .data_bits = 8U,
    .lsb_first = false,
};

static void print(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void print(const char *fmt, ...)
{
    char line[160];
    va_list ap;
    va_start(ap, fmt);
    size_t n = fmt_vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    uart_write(line, n);
}

static void report_previous_fault(void)
{
    fault_record_t f;
    if (!fault_take_last(&f)) {
        return;
    }
    char why[128];
    (void)fault_describe(f.cfsr, f.hfsr, why, sizeof why);
    print("\r\n*** previous boot ended in %s: %s\r\n", fault_exception_name(f.exception), why);
    print("    pc=0x%08x lr=0x%08x xpsr=0x%08x\r\n", (unsigned)f.pc, (unsigned)f.lr, (unsigned)f.xpsr);
    print("    r0=0x%08x r1=0x%08x r2=0x%08x r3=0x%08x r12=0x%08x\r\n", (unsigned)f.r0, (unsigned)f.r1, (unsigned)f.r2,
          (unsigned)f.r3, (unsigned)f.r12);
    print("    cfsr=0x%08x hfsr=0x%08x mmfar=0x%08x bfar=0x%08x\r\n", (unsigned)f.cfsr, (unsigned)f.hfsr,
          (unsigned)f.mmfar, (unsigned)f.bfar);
    print("    resolve: arm-none-eabi-addr2line -e tracker.elf 0x%08x\r\n", (unsigned)f.pc);
}

static void spi_setup(void)
{
    RCC->APB1ENR1 |= RCC_APB1ENR1_SPI2EN;
    (void)RCC->APB1ENR1;
    gpio_clock_enable(SPI_PORT);
    gpio_set_af(SPI_PORT, SPI_SCK_PIN, SPI_AF);
    gpio_set_af(SPI_PORT, SPI_MISO_PIN, SPI_AF);
    gpio_set_af(SPI_PORT, SPI_MOSI_PIN, SPI_AF);
    gpio_set_speed(SPI_PORT, SPI_SCK_PIN, GPIO_SPEED_HIGH);
    gpio_set_speed(SPI_PORT, SPI_MOSI_PIN, GPIO_SPEED_HIGH);
    gpio_set_pull(SPI_PORT, SPI_MISO_PIN, GPIO_PULL_DOWN);  // floating MISO reads as 0x00, not noise

    uint32_t actual = 0U;
    spi_status_t st = spi_init(SPI2, &spi_cfg, &actual);
    print("spi2: mode %u, %u bits, SCK %u Hz -> %s\r\n", spi_cfg.mode, spi_cfg.data_bits, (unsigned)actual,
          spi_status_str(st));
}

static void cmd_spi(void)
{
    uint8_t tx[16];
    uint8_t rx[16] = {0};
    for (unsigned i = 0; i < sizeof tx; i++) {
        tx[i] = (uint8_t)(0xA0U + i);
    }
    spi_status_t st = spi_transfer8(SPI2, tx, rx, sizeof tx);
    unsigned bad = 0U;
    for (unsigned i = 0; i < sizeof tx; i++) {
        if (rx[i] != tx[i]) {
            bad++;
        }
    }
    print("spi loopback: %s, %u/%u bytes match", spi_status_str(st), (unsigned)(sizeof tx - bad), (unsigned)sizeof tx);
    print(" (rx[0]=0x%02x rx[15]=0x%02x)\r\n", rx[0], rx[15]);
    if ((st == SPI_OK) && (bad == sizeof tx) && (rx[0] == 0x00U)) {
        print("  all zeros: is the PB15->PB14 jumper fitted?\r\n");
    }
}

static void cmd_regs(void)
{
    print("RCC  APB1ENR1=0x%08x AHB2ENR=0x%08x\r\n", (unsigned)RCC->APB1ENR1, (unsigned)RCC->AHB2ENR);
    print("GPIOB MODER=0x%08x AFRH=0x%08x OSPEEDR=0x%08x PUPDR=0x%08x\r\n", (unsigned)GPIOB->MODER,
          (unsigned)GPIOB->AFR[1], (unsigned)GPIOB->OSPEEDR, (unsigned)GPIOB->PUPDR);
    print("SPI2 CR1=0x%04x CR2=0x%04x SR=0x%04x\r\n", (unsigned)SPI2->CR1, (unsigned)SPI2->CR2, (unsigned)SPI2->SR);
}

static void cmd_fault(void)
{
    print("dividing by zero (DIV_0_TRP is on)...\r\n");
    volatile uint32_t zero = 0U;
    // cppcheck-suppress zerodiv ; deliberate: this command exists to demonstrate fault capture
    volatile uint32_t r = 100U / zero;
    (void)r;
}

static void cmd_payload(void)
{
    // Sample report (Madrid); decode the hex with: python3 tools/payload.py decode <hex>
    tracker_report_t r = {MSG_PERIODIC, FIX_3D, true, 42U, 1762946400U, 404167754, -37037902, 657,
                          48U,          271U,   6U,   9U,  14U,         3871U,     17,        -98};
    uint8_t buf[PAYLOAD_LEN];
    if (!payload_encode(&r, buf)) {
        uart_puts("payload: encode failed\r\n");
        return;
    }
    uart_puts("payload (20 B): ");
    for (unsigned i = 0; i < PAYLOAD_LEN; i++) {
        print("%02x", buf[i]);
    }
    uart_puts("\r\n");
}

static void cmd_help(void)
{
    uart_puts("commands: help | spi | mode <0-3> | regs | fault | stats | payload\r\n");
}

static void handle_line(char *line)
{
    if (strcmp(line, "help") == 0) {
        cmd_help();
    } else if (strcmp(line, "spi") == 0) {
        cmd_spi();
    } else if ((strncmp(line, "mode ", 5) == 0) && (line[5] >= '0') && (line[5] <= '3')) {
        spi_cfg.mode = (uint8_t)(line[5] - '0');
        spi_setup();
    } else if (strcmp(line, "regs") == 0) {
        cmd_regs();
    } else if (strcmp(line, "fault") == 0) {
        cmd_fault();
    } else if (strcmp(line, "payload") == 0) {
        cmd_payload();
    } else if (strcmp(line, "stats") == 0) {
        print("uptime %u ms, rx dropped %u, rx overruns %u\r\n", (unsigned)systick_ms(), (unsigned)uart_rx_dropped(),
              (unsigned)uart_rx_overruns());
    } else if (line[0] != '\0') {
        print("unknown command '%s'\r\n", line);
        cmd_help();
    }
}

int main(void)
{
    bool clock_ok = (rcc_clock_init_80mhz() == 0);
    systick_init(SystemCoreClock);
    fault_init();

    gpio_clock_enable(LED_PORT);
    gpio_set_mode(LED_PORT, LED_PIN, GPIO_MODE_OUTPUT);

    uart_init(clock_ok ? RCC_PCLK1_HZ : SystemCoreClock, 115200U);
    print("\r\nstm32l4-lte-m-tracker S1 | SYSCLK %u Hz%s\r\n", (unsigned)SystemCoreClock,
          clock_ok ? "" : " (PLL FAILED, running on MSI)");
    report_previous_fault();
    spi_setup();
    cmd_help();

    char line[64];
    size_t len = 0U;
    uint32_t last_blink = 0U;
    for (;;) {
        uint8_t c;
        while (uart_read(&c, 1U) == 1U) {
            if ((c == '\r') || (c == '\n')) {
                uart_puts("\r\n");
                line[len] = '\0';
                handle_line(line);
                len = 0U;
            } else if (len < (sizeof line - 1U)) {
                line[len++] = (char)c;
                uart_write((const char *)&c, 1U);  // echo
            }
        }
        if ((systick_ms() - last_blink) >= 500U) {
            last_blink = systick_ms();
            gpio_toggle(LED_PORT, LED_PIN);
        }
        __WFI();  // SysTick or UART/DMA interrupt wakes us up
    }
}
