// Vector table and reset handler for the STM32L476, written in C (no CubeMX startup file).
#include <stdint.h>

#include "stm32l4xx.h"

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;

int main(void);
void Reset_Handler(void);
void Default_Handler(void);

// Every handler is a weak alias of Default_Handler: a driver overrides it just by defining it.
#define WEAK_ALIAS __attribute__((weak, alias("Default_Handler")))
void NMI_Handler(void) WEAK_ALIAS;
void HardFault_Handler(void) WEAK_ALIAS;
void MemManage_Handler(void) WEAK_ALIAS;
void BusFault_Handler(void) WEAK_ALIAS;
void UsageFault_Handler(void) WEAK_ALIAS;
void SVC_Handler(void) WEAK_ALIAS;
void DebugMon_Handler(void) WEAK_ALIAS;
void PendSV_Handler(void) WEAK_ALIAS;
void SysTick_Handler(void) WEAK_ALIAS;
void DMA1_Channel6_IRQHandler(void) WEAK_ALIAS;
void DMA1_Channel7_IRQHandler(void) WEAK_ALIAS;
void SPI2_IRQHandler(void) WEAK_ALIAS;
void USART2_IRQHandler(void) WEAK_ALIAS;

#define IRQ(n) (16 + (n))
#define NUM_IRQS 82  // up to FPU_IRQn = 81 on the STM32L476

typedef void (*vector_t)(void);

// GCC range designators: every external IRQ defaults to Default_Handler, then the ones we use
// are overridden. Overriding an initialiser is intentional here.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverride-init"
__attribute__((section(".isr_vector"), used)) const vector_t vector_table[16 + NUM_IRQS] = {
    [0] = (vector_t)&_estack,
    [1] = Reset_Handler,
    [2] = NMI_Handler,
    [3] = HardFault_Handler,
    [4] = MemManage_Handler,
    [5] = BusFault_Handler,
    [6] = UsageFault_Handler,
    [11] = SVC_Handler,
    [12] = DebugMon_Handler,
    [14] = PendSV_Handler,
    [15] = SysTick_Handler,
    [16 ... 16 + NUM_IRQS - 1] = Default_Handler,
    [IRQ(DMA1_Channel6_IRQn)] = DMA1_Channel6_IRQHandler,
    [IRQ(DMA1_Channel7_IRQn)] = DMA1_Channel7_IRQHandler,
    [IRQ(SPI2_IRQn)] = SPI2_IRQHandler,
    [IRQ(USART2_IRQn)] = USART2_IRQHandler,
};
#pragma GCC diagnostic pop

void Reset_Handler(void)
{
    // FPU on (CP10/CP11 full access) before any code that might use floating point.
    SCB->CPACR |= (0xFU << 20);
    __DSB();
    __ISB();

    // Linker-script symbols delimit the sections, so comparing their addresses is intended.
    uint32_t *src = &_sidata;
    // cppcheck-suppress comparePointers
    for (uint32_t *dst = &_sdata; dst < &_edata;) {
        *dst++ = *src++;
    }
    // cppcheck-suppress comparePointers
    for (uint32_t *dst = &_sbss; dst < &_ebss;) {
        *dst++ = 0U;
    }

    (void)main();
    for (;;) {
    }
}

// An interrupt with no handler: stop here so the debugger shows which one (IPSR in xPSR).
void Default_Handler(void)
{
    for (;;) {
    }
}
