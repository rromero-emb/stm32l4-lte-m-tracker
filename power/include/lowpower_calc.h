#ifndef LOWPOWER_CALC_H
#define LOWPOWER_CALC_H

#include <stdbool.h>
#include <stdint.h>

// Pure arithmetic behind the LPTIM1 tick and tickless idle (no registers: unit-tested on the host).
//
// LPTIM1 runs from the LSE (32.768 kHz) with prescaler /8 -> 4096 counts/s, free-running over
// 0..0xFFFF. The RTOS tick is 1024 Hz, so a tick is exactly 4 counts and the counter period
// (65536 counts = 16 s) holds exactly 16384 ticks: the tick grid stays aligned across the wrap.
// The compare register (CMP) is moved along the grid; elapsed time is always computed from the
// counter, never by counting interrupts, so a late or missed interrupt cannot lose ticks.

#define LPT_COUNTS_PER_S 4096U
#define LPT_TICK_HZ 1024U
#define LPT_COUNTS_PER_TICK (LPT_COUNTS_PER_S / LPT_TICK_HZ)
// A CMP written less than this many counts ahead of the counter may take effect too late
// (the write crosses into the LPTIM clock domain first). 2 counts = 488 us.
#define LPT_MIN_LEAD_COUNTS 2U
// Longest tickless sleep: well under one counter period (16384 ticks).
#define LPT_MAX_SUPPRESSED_TICKS 16000U

// Below this many idle ticks Stop 2 is not worth it: waking up restarts the PLL (~ tens of us
// at 4 MHz) and the clock tree, so short gaps use Sleep (WFI, clocks running) instead.
#define PM_STOP2_MIN_TICKS 8U

typedef enum { PM_RUN = 0, PM_SLEEP, PM_STOP2 } pm_mode_t;

// Whole ticks between grid point `last` and counter value `now` (modulo the 16-bit counter).
uint32_t lpt_ticks_elapsed(uint16_t last, uint16_t now);

// CMP value for the grid point `ticks` ticks after `last`. If that point is not at least
// LPT_MIN_LEAD_COUNTS ahead of `now`, it moves forward by whole ticks until it is.
// *ticks_used (optional) receives the number of ticks after `last` actually programmed.
uint16_t lpt_compare_for(uint16_t last, uint16_t now, uint32_t ticks, uint32_t *ticks_used);

// After a tickless sleep that was programmed for `expected` ticks.
typedef struct {
    uint32_t step;      // ticks to add with vTaskStepTick()
    uint16_t last;      // new grid point
    bool tick_pending;  // the CMP interrupt will count the final tick (full sleep)
} lpt_wake_t;

// `expected` >= 1. If the whole sleep elapsed, step by expected - 1 and leave the last tick to
// the tick interrupt: FreeRTOS requires the tick that unblocks a task to go through
// xTaskIncrementTick(), and vTaskStepTick() must not move past the unblock time.
lpt_wake_t lpt_after_sleep(uint16_t last, uint16_t now, uint32_t expected);

// Low-power mode for an idle period. locks != 0 means a peripheral that does not run in Stop 2
// (UART with DMA, SPI transfer) is in use: only Sleep is allowed.
pm_mode_t pm_choose(uint32_t locks, uint32_t expected_ticks);

#endif
