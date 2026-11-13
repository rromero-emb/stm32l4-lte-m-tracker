// AT+QGPSLOC=2 parser and PSM timer encoding.
#include "gnss.h"
#include "psm.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_days_from_civil(void)
{
    TEST_ASSERT_EQUAL_INT32(0, gnss_days_from_civil(1970, 1U, 1U));
    TEST_ASSERT_EQUAL_INT32(20089, gnss_days_from_civil(2025, 1U, 1U));   // payload epoch 1735689600 / 86400
    TEST_ASSERT_EQUAL_INT32(11016, gnss_days_from_civil(2000, 2U, 29U));  // leap day
}

static void test_parse_madrid_fix(void)
{
    gnss_fix_t f;
    TEST_ASSERT_TRUE(
        gnss_parse_qgpsloc("+QGPSLOC: 112000.000,40.41677,-3.70379,1.2,657.0,3,271.30,48.6,26.2,121125,09", &f));
    TEST_ASSERT_EQUAL_UINT32(1762946400U, f.unix_time);  // 2025-11-12 11:20:00 UTC, same time as the console sample
    TEST_ASSERT_EQUAL_INT32(404167700, f.lat_e7);
    TEST_ASSERT_EQUAL_INT32(-37037900, f.lon_e7);
    TEST_ASSERT_EQUAL_INT32(657, f.alt_m);
    TEST_ASSERT_EQUAL_UINT16(12U, f.hdop_x10);
    TEST_ASSERT_EQUAL_UINT16(6U, f.hacc_m);
    TEST_ASSERT_EQUAL_UINT8(3U, f.fix);
    TEST_ASSERT_EQUAL_UINT16(271U, f.heading_deg);
    TEST_ASSERT_EQUAL_UINT16(49U, f.speed_kmh);
    TEST_ASSERT_EQUAL_UINT8(9U, f.nsat);
}

static void test_parse_southern_western_and_2d(void)
{
    gnss_fix_t f;
    TEST_ASSERT_TRUE(
        gnss_parse_qgpsloc("+QGPSLOC: 235959.000,-17.78629,-63.18117,3.4,416.0,2,0.00,0.0,0.0,311225,04", &f));
    TEST_ASSERT_EQUAL_INT32(-177862900, f.lat_e7);
    TEST_ASSERT_EQUAL_INT32(-631811700, f.lon_e7);
    TEST_ASSERT_EQUAL_UINT8(2U, f.fix);
    TEST_ASSERT_EQUAL_UINT32(1767225599U, f.unix_time);  // 2025-12-31 23:59:59 UTC
}

static void test_reject_malformed(void)
{
    gnss_fix_t f;
    TEST_ASSERT_FALSE(gnss_parse_qgpsloc("+CME ERROR: 516", &f));  // "not fixed now"
    TEST_ASSERT_FALSE(gnss_parse_qgpsloc("+QGPSLOC: 112000.000,40.41677,-3.70379,1.2,657.0,3", &f));
    TEST_ASSERT_FALSE(gnss_parse_qgpsloc("+QGPSLOC: 112000.000,91.0,-3.7,1.2,657.0,3,0.00,0.0,0.0,121126,09", &f));
    TEST_ASSERT_FALSE(gnss_parse_qgpsloc("+QGPSLOC: 112000.000,40.4,-180.5,1.2,657.0,3,0.00,0.0,0.0,121126,09", &f));
    TEST_ASSERT_FALSE(gnss_parse_qgpsloc("+QGPSLOC: 112000.000,40.4,-3.7,1.2,657.0,1,0.00,0.0,0.0,121126,09", &f));
    TEST_ASSERT_FALSE(gnss_parse_qgpsloc("+QGPSLOC: 112000.000,40.4,-3.7,1.2,657.0,3,0.00,0.0,0.0,121326,09", &f));
    TEST_ASSERT_FALSE(gnss_parse_qgpsloc("+QGPSLOC: 112000.000,40.4,,1.2,657.0,3,0.00,0.0,0.0,121126,09", &f));
    TEST_ASSERT_FALSE(gnss_parse_qgpsloc("+QGPSLOC: 112000.000,40.4x,-3.7,1.2,657.0,3,0.00,0.0,0.0,121126,09", &f));
    TEST_ASSERT_FALSE(
        gnss_parse_qgpsloc("+QGPSLOC: 112000.000,40.4,-3.7,1.2,657.0,3,0.00,0.0,0.0,121126,09,extra", &f));
}

static void test_t3412_encoding(void)
{
    char s[9];
    TEST_ASSERT_EQUAL_UINT32(86400U, psm_encode_t3412(86400U, s));  // 24 h
    TEST_ASSERT_EQUAL_STRING("00111000", s);                        // unit 1 h, value 24
    TEST_ASSERT_EQUAL_UINT32(3600U, psm_encode_t3412(3600U, s));
    TEST_ASSERT_EQUAL_STRING("00000110", s);                  // 10 min x 6 (tie with 1 h x 1: finer unit)
    TEST_ASSERT_EQUAL_UINT32(60U, psm_encode_t3412(60U, s));  // 2 s x 30
    TEST_ASSERT_EQUAL_STRING("01111110", s);
    TEST_ASSERT_EQUAL_UINT32(4U * 3600U, psm_encode_t3412(4U * 3600U, s));
    TEST_ASSERT_EQUAL_STRING("00011000", s);                                  // 10 min x 24
    TEST_ASSERT_EQUAL_UINT32(36000U * 4U, psm_encode_t3412(39U * 3600U, s));  // 40 h: 10 h x 4
    TEST_ASSERT_EQUAL_STRING("01000100", s);
}

static void test_t3412_rounds_up_and_clamps(void)
{
    char s[9];
    TEST_ASSERT_EQUAL_UINT32(90U, psm_encode_t3412(63U, s));  // 2 s x 32 does not fit: 30 s x 3
    TEST_ASSERT_EQUAL_STRING("10000011", s);
    TEST_ASSERT_EQUAL_UINT32(PSM_T3412_MAX_S, psm_encode_t3412(500U * 86400U, s));
    TEST_ASSERT_EQUAL_STRING("11011111", s);  // 320 h x 31
}

static void test_t3324_encoding(void)
{
    char s[9];
    TEST_ASSERT_EQUAL_UINT32(0U, psm_encode_t3324(0U, s));  // straight into PSM after the transfer
    TEST_ASSERT_EQUAL_STRING("00000000", s);
    TEST_ASSERT_EQUAL_UINT32(60U, psm_encode_t3324(60U, s));
    TEST_ASSERT_EQUAL_STRING("00011110", s);                   // 2 s x 30 (tie with 1 min x 1: finer unit)
    TEST_ASSERT_EQUAL_UINT32(120U, psm_encode_t3324(90U, s));  // 2 s x 45 does not fit -> 1 min x 2
    TEST_ASSERT_EQUAL_STRING("00100010", s);
    TEST_ASSERT_EQUAL_UINT32(PSM_T3324_MAX_S, psm_encode_t3324(24U * 3600U, s));
    TEST_ASSERT_EQUAL_STRING("01011111", s);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_days_from_civil);
    RUN_TEST(test_parse_madrid_fix);
    RUN_TEST(test_parse_southern_western_and_2d);
    RUN_TEST(test_reject_malformed);
    RUN_TEST(test_t3412_encoding);
    RUN_TEST(test_t3412_rounds_up_and_clamps);
    RUN_TEST(test_t3324_encoding);
    return UNITY_END();
}
