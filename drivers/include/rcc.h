#ifndef RCC_H
#define RCC_H

#include <stdint.h>

// SYSCLK = 80 MHz from PLL (MSI 4 MHz * 40 / 2). AHB = APB1 = APB2 = 80 MHz.
#define RCC_SYSCLK_HZ 80000000U
#define RCC_PCLK1_HZ RCC_SYSCLK_HZ
#define RCC_PCLK2_HZ RCC_SYSCLK_HZ

// Returns 0 on success, -1 if an oscillator or the PLL did not become ready in time.
int rcc_clock_init_80mhz(void);

#endif
