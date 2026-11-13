#ifndef BOARD_H
#define BOARD_H

// NUCLEO-L476RG wiring for session 3 (see the README table).
#include "stm32l4xx.h"

#define LED_PORT GPIOA
#define LED_PIN 5U  // LD2: on while the modem is powered

// BG95 on USART1 (uart_modem): PA9 TX (D8), PA10 RX (D2). PWRKEY driver on PA8 (D7). Carrier
// boards drive PWRKEY through an NPN transistor, so a high MCU pin pulls PWRKEY low.
#define MODEM_PWRKEY_PORT GPIOA
#define MODEM_PWRKEY_PIN 8U
#define MODEM_BAUD 115200U

// LIS2DH12 on SPI2: SCK PB13, MISO PB14, MOSI PB15 (AF5), CS PB12, INT1 -> PA0 (A0, EXTI0).
#define ACC_SPI SPI2
#define ACC_SPI_PORT GPIOB
#define ACC_SCK_PIN 13U
#define ACC_MISO_PIN 14U
#define ACC_MOSI_PIN 15U
#define ACC_SPI_AF 5U
#define ACC_CS_PORT GPIOB
#define ACC_CS_PIN 12U
#define ACC_INT_PORT GPIOA
#define ACC_INT_PIN 0U

// Console RX (PA3) doubles as EXTI3: a falling edge (start bit) wakes the MCU from Stop 2.
#define CONSOLE_RX_PIN 3U

#endif
