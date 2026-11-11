#include "fault.h"

#include <string.h>

#include "stm32l4xx.h"

__attribute__((section(".noinit"))) static fault_record_t last_fault;

void fault_init(void)
{
    SCB->SHCSR |= SCB_SHCSR_USGFAULTENA_Msk | SCB_SHCSR_BUSFAULTENA_Msk | SCB_SHCSR_MEMFAULTENA_Msk;
    SCB->CCR |= SCB_CCR_DIV_0_TRP_Msk;
}

bool fault_take_last(fault_record_t *out)
{
    if (last_fault.magic != FAULT_MAGIC) {
        return false;
    }
    *out = last_fault;
    last_fault.magic = 0U;
    return true;
}

// Called from the naked handlers with r0 = stacked frame, r1 = IPSR.
__attribute__((used, noreturn)) void fault_capture(const uint32_t *frame, uint32_t ipsr)
{
    last_fault.exception = ipsr & 0x1FFU;
    last_fault.r0 = frame[0];
    last_fault.r1 = frame[1];
    last_fault.r2 = frame[2];
    last_fault.r3 = frame[3];
    last_fault.r12 = frame[4];
    last_fault.lr = frame[5];
    last_fault.pc = frame[6];  // address of the faulting instruction (precise faults)
    last_fault.xpsr = frame[7];
    last_fault.cfsr = SCB->CFSR;
    last_fault.hfsr = SCB->HFSR;
    last_fault.mmfar = SCB->MMFAR;
    last_fault.bfar = SCB->BFAR;
    last_fault.magic = FAULT_MAGIC;

    // Halt here if a debugger is attached so the live state can be inspected; otherwise reset.
    if ((CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk) != 0U) {
        __BKPT(0);
    }
    NVIC_SystemReset();
}

// Picks MSP or PSP depending on EXC_RETURN bit 2 (which stack the exception frame was pushed to).
__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile(
        "tst lr, #4      \n"
        "ite eq          \n"
        "mrseq r0, msp   \n"
        "mrsne r0, psp   \n"
        "mrs r1, ipsr    \n"
        "b fault_capture \n");
}

void MemManage_Handler(void) __attribute__((alias("HardFault_Handler")));
void BusFault_Handler(void) __attribute__((alias("HardFault_Handler")));
void UsageFault_Handler(void) __attribute__((alias("HardFault_Handler")));
