#include "lowpower_calc.h"

_Static_assert((65536U % LPT_COUNTS_PER_TICK) == 0U, "the tick grid must divide the counter period");
_Static_assert(LPT_MAX_SUPPRESSED_TICKS *LPT_COUNTS_PER_TICK < 65536U - LPT_COUNTS_PER_TICK,
               "a tickless sleep must stay inside one counter period");

uint32_t lpt_ticks_elapsed(uint16_t last, uint16_t now)
{
    return (uint32_t)(uint16_t)(now - last) / LPT_COUNTS_PER_TICK;
}

uint16_t lpt_compare_for(uint16_t last, uint16_t now, uint32_t ticks, uint32_t *ticks_used)
{
    uint32_t elapsed_counts = (uint16_t)(now - last);
    uint32_t needed = elapsed_counts + LPT_MIN_LEAD_COUNTS;  // first count the CMP may sit on
    if ((ticks * LPT_COUNTS_PER_TICK) < needed) {
        ticks = (needed + LPT_COUNTS_PER_TICK - 1U) / LPT_COUNTS_PER_TICK;
    }
    if (ticks_used != 0) {
        *ticks_used = ticks;
    }
    return (uint16_t)(last + (ticks * LPT_COUNTS_PER_TICK));
}

lpt_wake_t lpt_after_sleep(uint16_t last, uint16_t now, uint32_t expected)
{
    lpt_wake_t w;
    uint32_t elapsed = lpt_ticks_elapsed(last, now);
    if (elapsed >= expected) {
        w.step = expected - 1U;
        w.tick_pending = true;
    } else {
        w.step = elapsed;  // woken early by another interrupt
        w.tick_pending = false;
    }
    w.last = (uint16_t)(last + (w.step * LPT_COUNTS_PER_TICK));
    return w;
}

pm_mode_t pm_choose(uint32_t locks, uint32_t expected_ticks)
{
    if ((locks != 0U) || (expected_ticks < PM_STOP2_MIN_TICKS)) {
        return PM_SLEEP;
    }
    return PM_STOP2;
}
