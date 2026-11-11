#include "gpio.h"

void gpio_clock_enable(GPIO_TypeDef *port)
{
    // GPIOA..GPIOH are 0x400 apart starting at GPIOA_BASE; AHB2ENR bits 0..7 in the same order.
    uint32_t index = ((uint32_t)port - GPIOA_BASE) / 0x400U;
    RCC->AHB2ENR |= (1UL << index);
    (void)RCC->AHB2ENR;  // read back so the clock is running before the port is touched
}

void gpio_set_mode(GPIO_TypeDef *port, uint8_t pin, gpio_mode_t mode)
{
    uint32_t shift = 2U * pin;
    port->MODER = (port->MODER & ~(3UL << shift)) | ((uint32_t)mode << shift);
}

void gpio_set_pull(GPIO_TypeDef *port, uint8_t pin, gpio_pull_t pull)
{
    uint32_t shift = 2U * pin;
    port->PUPDR = (port->PUPDR & ~(3UL << shift)) | ((uint32_t)pull << shift);
}

void gpio_set_speed(GPIO_TypeDef *port, uint8_t pin, gpio_speed_t speed)
{
    uint32_t shift = 2U * pin;
    port->OSPEEDR = (port->OSPEEDR & ~(3UL << shift)) | ((uint32_t)speed << shift);
}

void gpio_set_af(GPIO_TypeDef *port, uint8_t pin, uint8_t af)
{
    // AFR[0] = AFRL (pins 0..7), AFR[1] = AFRH (pins 8..15), 4 bits per pin.
    uint32_t reg = pin / 8U;
    uint32_t shift = 4U * (pin % 8U);
    port->AFR[reg] = (port->AFR[reg] & ~(0xFUL << shift)) | ((uint32_t)(af & 0xFU) << shift);
    gpio_set_mode(port, pin, GPIO_MODE_AF);
}
