#include "systick.h"

#include "stm32l4xx.h"

static volatile uint32_t ticks_ms;

void SysTick_Handler(void)
{
    ticks_ms++;
}

void systick_init(uint32_t core_hz)
{
    SysTick->LOAD = (core_hz / 1000U) - 1U;
    SysTick->VAL = 0U;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;
}

uint32_t systick_ms(void)
{
    return ticks_ms;
}

void systick_delay_ms(uint32_t ms)
{
    uint32_t start = ticks_ms;
    while ((ticks_ms - start) < ms) {  // unsigned subtraction: correct across wrap-around
        __WFI();
    }
}
