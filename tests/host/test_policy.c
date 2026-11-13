// Report policy: heartbeat, motion start/stop, rate limit, next wake-up.
#include "policy.h"
#include "unity.h"

static const policy_cfg_t cfg = {
    .period_still_s = 3600U, .period_moving_s = 300U, .still_after_s = 180U, .min_gap_s = 60U};
static policy_t p;

void setUp(void)
{
    policy_init(&p);
}
void tearDown(void) {}

static void test_first_report_at_boot_then_hourly_heartbeat(void)
{
    msg_type_t t;
    TEST_ASSERT_TRUE(policy_due(&p, &cfg, 5U, &t));
    TEST_ASSERT_EQUAL(MSG_HEARTBEAT, t);
    policy_reported(&p, 5U);
    TEST_ASSERT_FALSE(policy_due(&p, &cfg, 3604U, &t));
    TEST_ASSERT_EQUAL_UINT32(1U, policy_next_check_s(&p, &cfg, 3604U));
    TEST_ASSERT_EQUAL_UINT32(3600U, policy_next_check_s(&p, &cfg, 5U));
    TEST_ASSERT_TRUE(policy_due(&p, &cfg, 3605U, &t));
    TEST_ASSERT_EQUAL(MSG_HEARTBEAT, t);
}

static void test_motion_start_is_rate_limited(void)
{
    msg_type_t t;
    policy_reported(&p, 1000U);
    policy_on_motion(&p, 1010U);  // 10 s after the last report
    TEST_ASSERT_FALSE(policy_due(&p, &cfg, 1010U, &t));
    TEST_ASSERT_EQUAL_UINT32(50U, policy_next_check_s(&p, &cfg, 1010U));
    TEST_ASSERT_TRUE(policy_due(&p, &cfg, 1060U, &t));
    TEST_ASSERT_EQUAL(MSG_MOTION_START, t);
}

static void test_moving_reports_every_moving_period(void)
{
    msg_type_t t;
    policy_reported(&p, 0U);
    policy_on_motion(&p, 100U);
    TEST_ASSERT_TRUE(policy_due(&p, &cfg, 100U, &t));
    policy_reported(&p, 100U);
    for (uint32_t s = 100U; s <= 400U; s += 50U) {
        policy_on_motion(&p, s);  // keeps moving
    }
    TEST_ASSERT_TRUE(policy_due(&p, &cfg, 400U, &t));
    TEST_ASSERT_EQUAL(MSG_PERIODIC, t);
}

static void test_motion_stop_after_quiet_time(void)
{
    msg_type_t t;
    policy_reported(&p, 0U);
    policy_on_motion(&p, 100U);
    TEST_ASSERT_TRUE(policy_due(&p, &cfg, 100U, &t));
    policy_reported(&p, 100U);
    TEST_ASSERT_EQUAL_UINT32(180U, policy_next_check_s(&p, &cfg, 100U));  // still_after < moving period
    TEST_ASSERT_FALSE(policy_due(&p, &cfg, 279U, &t));
    TEST_ASSERT_TRUE(policy_due(&p, &cfg, 280U, &t));
    TEST_ASSERT_EQUAL(MSG_MOTION_STOP, t);
    policy_reported(&p, 280U);
    TEST_ASSERT_FALSE(p.moving);
    TEST_ASSERT_EQUAL_UINT32(3600U, policy_next_check_s(&p, &cfg, 280U));
}

static void test_repeated_motion_while_moving_is_not_a_new_event(void)
{
    msg_type_t t;
    policy_reported(&p, 0U);
    policy_on_motion(&p, 100U);
    TEST_ASSERT_TRUE(policy_due(&p, &cfg, 100U, &t));
    policy_reported(&p, 100U);
    policy_on_motion(&p, 120U);
    TEST_ASSERT_FALSE(policy_due(&p, &cfg, 170U, &t));
}

static void test_clock_wrap(void)
{
    msg_type_t t;
    policy_reported(&p, 0xFFFFFF00U);
    TEST_ASSERT_FALSE(policy_due(&p, &cfg, 0x00000100U, &t));  // 512 s later
    TEST_ASSERT_TRUE(policy_due(&p, &cfg, 0xFFFFFF00U + 3600U, &t));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_first_report_at_boot_then_hourly_heartbeat);
    RUN_TEST(test_motion_start_is_rate_limited);
    RUN_TEST(test_moving_reports_every_moving_period);
    RUN_TEST(test_motion_stop_after_quiet_time);
    RUN_TEST(test_repeated_motion_while_moving_is_not_a_new_event);
    RUN_TEST(test_clock_wrap);
    return UNITY_END();
}
