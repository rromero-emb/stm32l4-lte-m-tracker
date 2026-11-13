// LPTIM tick grid and tickless-idle arithmetic.
#include "lowpower_calc.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_tick_is_four_counts_and_grid_divides_period(void)
{
    TEST_ASSERT_EQUAL_UINT(4U, LPT_COUNTS_PER_TICK);
    TEST_ASSERT_EQUAL_UINT(0U, 65536U % LPT_COUNTS_PER_TICK);
}

static void test_elapsed_counts_whole_ticks_only(void)
{
    TEST_ASSERT_EQUAL_UINT(0U, lpt_ticks_elapsed(100U, 103U));
    TEST_ASSERT_EQUAL_UINT(1U, lpt_ticks_elapsed(100U, 104U));
    TEST_ASSERT_EQUAL_UINT(2U, lpt_ticks_elapsed(100U, 111U));
}

static void test_elapsed_across_counter_wrap(void)
{
    // 0xFFF8 -> 0x0008 is 16 counts = 4 ticks
    TEST_ASSERT_EQUAL_UINT(4U, lpt_ticks_elapsed(0xFFF8U, 0x0008U));
}

static void test_compare_on_grid_when_far_enough(void)
{
    uint32_t used = 0U;
    TEST_ASSERT_EQUAL_HEX16(0x0104U, lpt_compare_for(0x0100U, 0x0100U, 1U, &used));
    TEST_ASSERT_EQUAL_UINT(1U, used);
    TEST_ASSERT_EQUAL_HEX16(0x0100U + 400U, lpt_compare_for(0x0100U, 0x0101U, 100U, &used));
    TEST_ASSERT_EQUAL_UINT(100U, used);
}

static void test_compare_too_close_moves_forward_whole_ticks(void)
{
    // last tick at 0x100, counter already at 0x103: 0x104 is only 1 count ahead -> skip to 0x108
    uint32_t used = 0U;
    uint16_t cmp = lpt_compare_for(0x0100U, 0x0103U, 1U, &used);
    TEST_ASSERT_EQUAL_HEX16(0x0108U, cmp);
    TEST_ASSERT_EQUAL_UINT(2U, used);
    TEST_ASSERT_EQUAL_UINT(0U, (uint16_t)(cmp - 0x0100U) % LPT_COUNTS_PER_TICK);  // still on the grid
}

static void test_compare_after_long_isr_latency(void)
{
    // interrupt served 10 ticks late: the next compare is the first grid point ahead of now
    uint32_t used = 0U;
    uint16_t cmp = lpt_compare_for(0x0000U, 41U, 1U, &used);
    TEST_ASSERT_EQUAL_UINT16(44U, cmp);
    TEST_ASSERT_EQUAL_UINT(11U, used);
}

static void test_compare_never_hits_0xffff(void)
{
    // CMP must be below ARR (0xFFFF). Grid points are multiples of 4 from an aligned start.
    for (uint32_t now = 0U; now < 65536U; now += 3U) {
        uint16_t last = (uint16_t)(now & ~3U);
        for (uint32_t t = 1U; t < 20U; t++) {
            TEST_ASSERT_NOT_EQUAL(0xFFFFU, lpt_compare_for(last, (uint16_t)now, t, 0));
        }
    }
}

static void test_full_sleep_leaves_last_tick_to_interrupt(void)
{
    lpt_wake_t w = lpt_after_sleep(0x0000U, 400U, 100U);  // exactly 100 ticks
    TEST_ASSERT_TRUE(w.tick_pending);
    TEST_ASSERT_EQUAL_UINT(99U, w.step);
    TEST_ASSERT_EQUAL_UINT16(396U, w.last);
    // the interrupt then sees one more elapsed tick
    TEST_ASSERT_EQUAL_UINT(1U, lpt_ticks_elapsed(w.last, 400U));
}

static void test_overslept_still_steps_only_to_expected_minus_one(void)
{
    lpt_wake_t w = lpt_after_sleep(0xFFF0U, 0x0200U, 100U);  // 0x210 counts = 132 ticks
    TEST_ASSERT_TRUE(w.tick_pending);
    TEST_ASSERT_EQUAL_UINT(99U, w.step);
    TEST_ASSERT_EQUAL_UINT(33U, lpt_ticks_elapsed(w.last, 0x0200U));  // counted by the interrupt
}

static void test_early_wake_steps_elapsed_ticks(void)
{
    lpt_wake_t w = lpt_after_sleep(0x1000U, 0x1000U + 4U * 37U + 3U, 1000U);
    TEST_ASSERT_FALSE(w.tick_pending);
    TEST_ASSERT_EQUAL_UINT(37U, w.step);
    TEST_ASSERT_EQUAL_UINT16(0x1000U + 4U * 37U, w.last);
}

static void test_mode_choice(void)
{
    TEST_ASSERT_EQUAL(PM_STOP2, pm_choose(0U, 1000U));
    TEST_ASSERT_EQUAL(PM_SLEEP, pm_choose(0x1U, 1000U));                  // UART in use
    TEST_ASSERT_EQUAL(PM_SLEEP, pm_choose(0U, PM_STOP2_MIN_TICKS - 1U));  // too short to pay off
    TEST_ASSERT_EQUAL(PM_STOP2, pm_choose(0U, PM_STOP2_MIN_TICKS));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_tick_is_four_counts_and_grid_divides_period);
    RUN_TEST(test_elapsed_counts_whole_ticks_only);
    RUN_TEST(test_elapsed_across_counter_wrap);
    RUN_TEST(test_compare_on_grid_when_far_enough);
    RUN_TEST(test_compare_too_close_moves_forward_whole_ticks);
    RUN_TEST(test_compare_after_long_isr_latency);
    RUN_TEST(test_compare_never_hits_0xffff);
    RUN_TEST(test_full_sleep_leaves_last_tick_to_interrupt);
    RUN_TEST(test_overslept_still_steps_only_to_expected_minus_one);
    RUN_TEST(test_early_wake_steps_elapsed_ticks);
    RUN_TEST(test_mode_choice);
    return UNITY_END();
}
