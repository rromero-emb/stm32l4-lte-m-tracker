#ifndef PM_H
#define PM_H

#include <stdint.h>

#include "lowpower_calc.h"
#include "rcc.h"

// Power management: the FreeRTOS tick on LPTIM1 and tickless idle into Sleep or Stop 2.
//
// Run:    80 MHz from the PLL, tick interrupt every 1/1024 s.
// Sleep:  WFI, clocks running. Used when a lock is held or the idle gap is short.
// Stop 2: all high-speed clocks off, SRAM and registers kept, LPTIM1 (LSE) and EXTI still
//         running. Woken by the LPTIM1 compare (next task timeout) or an EXTI line (motion,
//         console RX edge). Datasheet: ~1 uA for the MCU itself.
//
// A lock is a bit per client that needs a peripheral which stops in Stop 2.
#define PM_LOCK_MODEM 0x1U    // USART1 + DMA receiving from the BG95
#define PM_LOCK_CONSOLE 0x2U  // USART2 + DMA: someone is typing

void pm_lock(uint32_t who);
void pm_unlock(uint32_t who);
void pm_lock_from_isr(uint32_t who);
uint32_t pm_locks(void);

typedef struct {
    uint32_t stop2_entries;
    uint32_t sleep_entries;
    uint64_t stop2_ticks;  // time spent in each mode, in RTOS ticks (1/1024 s)
    uint64_t sleep_ticks;
    uint32_t clock_restore_failures;
} pm_stats_t;

void pm_get_stats(pm_stats_t *out);
rcc_lsclk_t pm_lsclk(void);

// Uptime from the 64-bit tick count: no wrap in practice. ms wraps every 49.7 days, which
// the AT layer handles with unsigned differences.
uint64_t pm_ticks64(void);
uint32_t pm_uptime_ms(void);
uint32_t pm_uptime_s(void);

#endif
