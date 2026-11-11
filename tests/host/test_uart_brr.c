#include "uart.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_brr_values(void)
{
    TEST_ASSERT_EQUAL_UINT32(694U, uart_brr(80000000U, 115200U));  // 694.44 -> 694 (0.06 % error)
    TEST_ASSERT_EQUAL_UINT32(35U, uart_brr(4000000U, 115200U));    // 34.72 -> 35 on MSI (0.8 % error)
    TEST_ASSERT_EQUAL_UINT32(8333U, uart_brr(80000000U, 9600U));
}

static void test_baud_error_below_2_percent(void)
{
    const uint32_t clocks[] = {4000000U, 16000000U, 80000000U};
    const uint32_t bauds[] = {9600U, 57600U, 115200U, 921600U};
    for (unsigned c = 0; c < 3U; c++) {
        for (unsigned b = 0; b < 4U; b++) {
            if ((clocks[c] / bauds[b] < 16U) || ((clocks[c] == 16000000U) && (bauds[b] == 921600U))) {
                continue;  // BRR must be >= 16; 16 MHz / 921600 is covered by the test below
            }
            uint32_t brr = uart_brr(clocks[c], bauds[b]);
            double actual = (double)clocks[c] / brr;
            double err = (actual - bauds[b]) / bauds[b];
            TEST_ASSERT_TRUE_MESSAGE(err < 0.02 && err > -0.02, "baud error above 2 %");
        }
    }
}

// A real limit, not a driver bug: 16 MHz / 921600 = 17.36 -> BRR 17 -> 941 kbaud, +2.1 %.
// Above the ~2 % a receiver tolerates, so at that clock use OVER8 or a faster/other kernel clock.
static void test_16mhz_cannot_do_921600_with_over16(void)
{
    uint32_t brr = uart_brr(16000000U, 921600U);
    double err = ((double)16000000U / brr - 921600.0) / 921600.0;
    TEST_ASSERT_EQUAL_UINT32(17U, brr);
    TEST_ASSERT_TRUE(err > 0.02);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_brr_values);
    RUN_TEST(test_baud_error_below_2_percent);
    RUN_TEST(test_16mhz_cannot_do_921600_with_over16);
    return UNITY_END();
}
