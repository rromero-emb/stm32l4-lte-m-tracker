#include "fault.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_divide_by_zero_escalated(void)
{
    char buf[64];
    // UsageFault DIVBYZERO (CFSR bit 25); HFSR.FORCED set when it was escalated to HardFault.
    fault_describe(1UL << 25, 1UL << 30, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("DIVBYZERO, FORCED", buf);
}

static void test_precise_bus_fault(void)
{
    char buf[64];
    fault_describe((1UL << 9) | (1UL << 15), 0U, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("PRECISERR, BFARVALID", buf);
}

static void test_no_bits(void)
{
    char buf[16];
    fault_describe(0U, 0U, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("none", buf);
}

static void test_small_buffer_stays_terminated(void)
{
    char buf[12];
    fault_describe((1UL << 16) | (1UL << 25), 0U, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("UNDEFINSTR", buf);  // ", DIVBYZERO" does not fit and is dropped whole
}

static void test_exception_names(void)
{
    TEST_ASSERT_EQUAL_STRING("HardFault", fault_exception_name(3U));
    TEST_ASSERT_EQUAL_STRING("UsageFault", fault_exception_name(6U));
    TEST_ASSERT_EQUAL_STRING("unknown", fault_exception_name(11U));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_divide_by_zero_escalated);
    RUN_TEST(test_precise_bus_fault);
    RUN_TEST(test_no_bits);
    RUN_TEST(test_small_buffer_stays_terminated);
    RUN_TEST(test_exception_names);
    return UNITY_END();
}
