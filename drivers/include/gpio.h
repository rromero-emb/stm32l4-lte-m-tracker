#ifndef GPIO_H
#define GPIO_H

#include <stdbool.h>
#include <stdint.h>

#include "stm32l4xx.h"

typedef enum { GPIO_MODE_INPUT = 0, GPIO_MODE_OUTPUT = 1, GPIO_MODE_AF = 2, GPIO_MODE_ANALOG = 3 } gpio_mode_t;
typedef enum { GPIO_PULL_NONE = 0, GPIO_PULL_UP = 1, GPIO_PULL_DOWN = 2 } gpio_pull_t;
typedef enum { GPIO_SPEED_LOW = 0, GPIO_SPEED_MEDIUM = 1, GPIO_SPEED_HIGH = 2, GPIO_SPEED_VERY_HIGH = 3 } gpio_speed_t;

// Enables the port clock (RCC->AHB2ENR) for GPIOA..GPIOH.
void gpio_clock_enable(GPIO_TypeDef *port);
void gpio_set_mode(GPIO_TypeDef *port, uint8_t pin, gpio_mode_t mode);
void gpio_set_pull(GPIO_TypeDef *port, uint8_t pin, gpio_pull_t pull);
void gpio_set_speed(GPIO_TypeDef *port, uint8_t pin, gpio_speed_t speed);
// Alternate function number (0..15) from the datasheet's AF table, then mode = AF.
void gpio_set_af(GPIO_TypeDef *port, uint8_t pin, uint8_t af);

static inline void gpio_write(GPIO_TypeDef *port, uint8_t pin, bool high)
{
    // BSRR: atomic set (low half) / reset (high half), no read-modify-write.
    port->BSRR = high ? (1UL << pin) : (1UL << (pin + 16U));
}

static inline bool gpio_read(const GPIO_TypeDef *port, uint8_t pin)
{
    return (port->IDR & (1UL << pin)) != 0U;
}

static inline void gpio_toggle(GPIO_TypeDef *port, uint8_t pin)
{
    gpio_write(port, pin, (port->ODR & (1UL << pin)) == 0U);
}

#endif
