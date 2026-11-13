// FreeRTOS tick from LPTIM1 and tickless idle with Stop 2 (configUSE_TICKLESS_IDLE = 2: the
// port's SysTick tick and its tickless code are replaced by the two functions below).
// The arithmetic is in lowpower_calc.c and unit-tested on the host.
#include "pm.h"

#include <stdbool.h>

#include "FreeRTOS.h"
#include "stm32l4xx.h"
#include "task.h"

#define CMPOK_TIMEOUT 10000U

static volatile uint32_t locks;
static volatile uint16_t grid;  // counter value of the last counted tick (multiple of 4)
static volatile uint64_t ticks64;
static bool cmp_in_sync;  // a CMP write is being synchronised into the LPTIM clock domain
static rcc_lsclk_t lsclk = RCC_LSCLK_NONE;
static pm_stats_t stats;

// ---------------------------------------------------------------- locks

void pm_lock(uint32_t who)
{
    taskENTER_CRITICAL();
    locks |= who;
    taskEXIT_CRITICAL();
}

void pm_unlock(uint32_t who)
{
    taskENTER_CRITICAL();
    locks &= ~who;
    taskEXIT_CRITICAL();
}

void pm_lock_from_isr(uint32_t who)
{
    UBaseType_t s = taskENTER_CRITICAL_FROM_ISR();
    locks |= who;
    taskEXIT_CRITICAL_FROM_ISR(s);
}

uint32_t pm_locks(void)
{
    return locks;
}

// ---------------------------------------------------------------- LPTIM1

// The counter runs on the asynchronous LSE clock: read it until two reads agree (RM0351 31.4.12).
static uint16_t cnt_read(void)
{
    uint32_t a;
    uint32_t b;
    do {
        a = LPTIM1->CNT;
        b = LPTIM1->CNT;
    } while (a != b);
    return (uint16_t)a;
}

// A new CMP value may only be written once the previous write has reached the LPTIM clock
// domain (CMPOK). At one write per tick it has always finished; after a tickless wake-up the
// wait is at most a few LSE cycles.
static void cmp_write(uint16_t value)
{
    if (cmp_in_sync) {
        for (uint32_t i = 0; ((LPTIM1->ISR & LPTIM_ISR_CMPOK) == 0U) && (i < CMPOK_TIMEOUT); i++) {
        }
    }
    LPTIM1->ICR = LPTIM_ICR_CMPOKCF;
    LPTIM1->CMP = value;
    cmp_in_sync = true;
}

void vPortSetupTimerInterrupt(void)
{
    lsclk = rcc_lsclk_init();
    uint32_t sel = (lsclk == RCC_LSCLK_LSE) ? 3U : 1U;  // CCIPR.LPTIM1SEL: 11 = LSE, 01 = LSI
    RCC->CCIPR = (RCC->CCIPR & ~RCC_CCIPR_LPTIM1SEL) | (sel << RCC_CCIPR_LPTIM1SEL_Pos);
    RCC->APB1ENR1 |= RCC_APB1ENR1_LPTIM1EN;
    RCC->APB1SMENR1 |= RCC_APB1SMENR1_LPTIM1SMEN;
    (void)RCC->APB1ENR1;

    // CFGR and IER can only be written while the timer is disabled; ARR and CMP only while enabled.
    LPTIM1->CR = 0U;
    LPTIM1->CFGR = 3U << LPTIM_CFGR_PRESC_Pos;  // /8: 4096 counts/s
    LPTIM1->IER = LPTIM_IER_CMPMIE;
    LPTIM1->CR = LPTIM_CR_ENABLE;
    LPTIM1->ARR = 0xFFFFU;
    for (uint32_t i = 0; ((LPTIM1->ISR & LPTIM_ISR_ARROK) == 0U) && (i < CMPOK_TIMEOUT); i++) {
    }
    LPTIM1->ICR = LPTIM_ICR_ARROKCF;
    grid = 0U;
    cmp_write(LPT_COUNTS_PER_TICK);
    LPTIM1->CR = LPTIM_CR_ENABLE | LPTIM_CR_CNTSTRT;

    EXTI->IMR2 |= EXTI_IMR2_IM32;  // LPTIM1 event line: wakes the core from Stop 2
    NVIC_SetPriority(LPTIM1_IRQn, configLIBRARY_LOWEST_INTERRUPT_PRIORITY);
    NVIC_EnableIRQ(LPTIM1_IRQn);
}

void LPTIM1_IRQHandler(void)
{
    LPTIM1->ICR = LPTIM_ICR_CMPMCF;
    UBaseType_t s = taskENTER_CRITICAL_FROM_ISR();
    uint16_t now = cnt_read();
    uint32_t n = lpt_ticks_elapsed(grid, now);
    BaseType_t switch_needed = pdFALSE;
    if (n > 0U) {
        grid = (uint16_t)(grid + (n * LPT_COUNTS_PER_TICK));
        ticks64 += n;
        while (n-- > 0U) {
            if (xTaskIncrementTick() != pdFALSE) {
                switch_needed = pdTRUE;
            }
        }
    }
    cmp_write(lpt_compare_for(grid, now, 1U, NULL));
    taskEXIT_CRITICAL_FROM_ISR(s);
    portYIELD_FROM_ISR(switch_needed);
}

// ---------------------------------------------------------------- tickless idle

void vPortSuppressTicksAndSleep(TickType_t expected)
{
    if (expected > LPT_MAX_SUPPRESSED_TICKS) {
        expected = LPT_MAX_SUPPRESSED_TICKS;
    }
    // PRIMASK, not BASEPRI: WFI still wakes on a pending interrupt, which then runs only after
    // the tick count has been corrected below.
    __disable_irq();
    __DSB();
    __ISB();
    if (eTaskConfirmSleepModeStatus() == eAbortSleep) {
        __enable_irq();
        return;
    }
    uint16_t now = cnt_read();
    uint32_t ticks_used = 0U;
    uint16_t cmp = lpt_compare_for(grid, now, expected, &ticks_used);
    if (ticks_used != expected) {
        __enable_irq();  // the wake-up time is already (almost) here: let the tick interrupt run
        return;
    }
    cmp_write(cmp);
    LPTIM1->ICR = LPTIM_ICR_CMPMCF;
    NVIC_ClearPendingIRQ(LPTIM1_IRQn);

    pm_mode_t mode = pm_choose(locks, expected);
    if (mode == PM_STOP2) {
        PWR->CR1 = (PWR->CR1 & ~PWR_CR1_LPMS) | PWR_CR1_LPMS_STOP2;
        SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
    }
    __DSB();
    __WFI();
    __ISB();
    if (mode == PM_STOP2) {
        SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
        if (rcc_restore_after_stop() != 0) {
            stats.clock_restore_failures++;
        }
    }

    now = cnt_read();
    lpt_wake_t w = lpt_after_sleep(grid, now, expected);
    grid = w.last;
    ticks64 += w.step;
    if (w.tick_pending) {
        NVIC_SetPendingIRQ(LPTIM1_IRQn);  // counts the final tick(s) as soon as interrupts are on
    } else {
        cmp_write(lpt_compare_for(grid, now, 1U, NULL));
    }
    if (w.step > 0U) {
        vTaskStepTick(w.step);
    }
    if (mode == PM_STOP2) {
        stats.stop2_entries++;
        stats.stop2_ticks += w.step;
    } else {
        stats.sleep_entries++;
        stats.sleep_ticks += w.step;
    }
    __enable_irq();
}

// ---------------------------------------------------------------- time and statistics

uint64_t pm_ticks64(void)
{
    taskENTER_CRITICAL();
    uint64_t t = ticks64;
    taskEXIT_CRITICAL();
    return t;
}

uint32_t pm_uptime_ms(void)
{
    return (uint32_t)((pm_ticks64() * 1000U) >> 10);
}

uint32_t pm_uptime_s(void)
{
    return (uint32_t)(pm_ticks64() >> 10);
}

void pm_get_stats(pm_stats_t *out)
{
    taskENTER_CRITICAL();
    *out = stats;
    taskEXIT_CRITICAL();
}

rcc_lsclk_t pm_lsclk(void)
{
    return lsclk;
}
