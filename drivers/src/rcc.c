// Clock tree: MSI 4 MHz (reset default) -> PLL (M=1, N=40, R=2) -> SYSCLK 80 MHz.
// RM0351 6.2: VCO input 4 MHz (allowed 4..16), VCO output 160 MHz (64..344), R output 80 MHz.
#include "rcc.h"

#include "stm32l4xx.h"

uint32_t SystemCoreClock = 4000000U;  // CMSIS: MSI 4 MHz after reset

#define READY_TIMEOUT 100000U

static int wait_set(const volatile uint32_t *reg, uint32_t mask)
{
    for (uint32_t i = 0; i < READY_TIMEOUT; i++) {
        if ((*reg & mask) == mask) {
            return 0;
        }
    }
    return -1;
}

int rcc_clock_init_80mhz(void)
{
    // Voltage scaling range 1 (reset default) is required above 26 MHz.
    RCC->APB1ENR1 |= RCC_APB1ENR1_PWREN;
    (void)RCC->APB1ENR1;  // read back: the enable takes effect before PWR is accessed
    PWR->CR1 = (PWR->CR1 & ~PWR_CR1_VOS) | PWR_CR1_VOS_0;
    for (uint32_t i = 0; (PWR->SR2 & PWR_SR2_VOSF) != 0U; i++) {
        if (i > READY_TIMEOUT) {
            return -1;
        }
    }

    // Flash: 4 wait states at 80 MHz in range 1 (RM0351 table 12), prefetch and caches on.
    // Must be set BEFORE raising the clock.
    FLASH->ACR = FLASH_ACR_LATENCY_4WS | FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN;
    if ((FLASH->ACR & FLASH_ACR_LATENCY) != FLASH_ACR_LATENCY_4WS) {
        return -1;
    }

    // MSI is already running at 4 MHz after reset.
    if (wait_set(&RCC->CR, RCC_CR_MSIRDY) != 0) {
        return -1;
    }

    // The PLL can only be configured while it is off.
    RCC->CR &= ~RCC_CR_PLLON;
    for (uint32_t i = 0; (RCC->CR & RCC_CR_PLLRDY) != 0U; i++) {
        if (i > READY_TIMEOUT) {
            return -1;
        }
    }
    RCC->PLLCFGR = RCC_PLLCFGR_PLLSRC_MSI | (0U << RCC_PLLCFGR_PLLM_Pos)  // M = 1
                   | (40U << RCC_PLLCFGR_PLLN_Pos)                        // N = 40
                   | (0U << RCC_PLLCFGR_PLLR_Pos)                         // R = 2
                   | RCC_PLLCFGR_PLLREN;
    RCC->CR |= RCC_CR_PLLON;
    if (wait_set(&RCC->CR, RCC_CR_PLLRDY) != 0) {
        return -1;
    }

    // AHB/APB1/APB2 prescalers = 1, then switch SYSCLK to the PLL.
    RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2 | RCC_CFGR_SW)) | RCC_CFGR_SW_PLL;
    for (uint32_t i = 0; (RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL; i++) {
        if (i > READY_TIMEOUT) {
            return -1;
        }
    }

    SystemCoreClock = RCC_SYSCLK_HZ;
    return 0;
}
