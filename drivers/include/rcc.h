#ifndef RCC_H
#define RCC_H

#include <stdint.h>

// SYSCLK = 80 MHz from PLL (MSI 4 MHz * 40 / 2). AHB = APB1 = APB2 = 80 MHz.
#define RCC_SYSCLK_HZ 80000000U
#define RCC_PCLK1_HZ RCC_SYSCLK_HZ
#define RCC_PCLK2_HZ RCC_SYSCLK_HZ

// Returns 0 on success, -1 if an oscillator or the PLL did not become ready in time.
int rcc_clock_init_80mhz(void);

typedef enum { RCC_LSCLK_LSE = 0, RCC_LSCLK_LSI = 1, RCC_LSCLK_NONE = 2 } rcc_lsclk_t;

// Low-speed clock for LPTIM1 (the RTOS tick, runs in Stop 2): the 32.768 kHz LSE crystal (X2 on
// the NUCLEO-L476RG), or the internal ~32 kHz LSI if the crystal does not start. LSI is about
// 2.3 % slow for a 32768 Hz time base and drifts with temperature: the console reports it.
rcc_lsclk_t rcc_lsclk_init(void);

// After Stop 2 the core runs from MSI (4 MHz) and the PLL is off: switch back to 80 MHz.
// PLLCFGR, voltage range and flash wait states are kept across Stop 2.
int rcc_restore_after_stop(void);

#endif
