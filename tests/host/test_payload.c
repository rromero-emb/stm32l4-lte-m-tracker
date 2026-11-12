#include <stdlib.h>
#include <string.h>

#include "payload.h"
#include "unity.h"

static tracker_report_t sample(void)
{
    tracker_report_t r = {
        .type = MSG_PERIODIC,
        .fix = FIX_3D,
        .moving = true,
        .seq = 42U,
        .unix_time = 1762946400U,
        .lat_e7 = 404167754,  // Madrid
        .lon_e7 = -37037902,
        .alt_m = 657,
        .speed_kmh = 48U,
        .heading_deg = 271U,
        .hacc_m = 6U,
        .nsat = 9U,
        .ttff_s = 14U,
        .vbat_mv = 3871U,
        .temp_c = 17,
        .rsrp_dbm = -98,
    };
    return r;
}

void setUp(void) {}
void tearDown(void) {}

static void test_roundtrip_within_quantisation(void)
{
    tracker_report_t in = sample();
    tracker_report_t out;
    uint8_t buf[PAYLOAD_LEN];
    TEST_ASSERT_TRUE(payload_encode(&in, buf));
    TEST_ASSERT_TRUE(payload_decode(buf, &out));
    TEST_ASSERT_EQUAL(in.type, out.type);
    TEST_ASSERT_EQUAL(in.fix, out.fix);
    TEST_ASSERT_EQUAL(in.moving, out.moving);
    TEST_ASSERT_EQUAL_UINT8(in.seq, out.seq);
    TEST_ASSERT_EQUAL_UINT32(in.unix_time, out.unix_time);  // exact to the second
    // 24-bit lat/lon: steps of 107e-7 deg (lat) / 215e-7 deg (lon): about 1.2 m / 2.4 m at the equator
    TEST_ASSERT_INT32_WITHIN(54, in.lat_e7, out.lat_e7);
    TEST_ASSERT_INT32_WITHIN(108, in.lon_e7, out.lon_e7);
    TEST_ASSERT_INT32_WITHIN(4, in.alt_m, out.alt_m);
    TEST_ASSERT_EQUAL_UINT16(in.speed_kmh, out.speed_kmh);
    TEST_ASSERT_UINT16_WITHIN(6U, in.heading_deg, out.heading_deg);
    TEST_ASSERT_UINT16_WITHIN(1U, in.hacc_m, out.hacc_m);
    TEST_ASSERT_EQUAL_UINT8(in.nsat, out.nsat);
    TEST_ASSERT_UINT16_WITHIN(1U, in.ttff_s, out.ttff_s);
    TEST_ASSERT_UINT16_WITHIN(10U, in.vbat_mv, out.vbat_mv);
    TEST_ASSERT_EQUAL_INT16(in.temp_c, out.temp_c);
    TEST_ASSERT_EQUAL_INT16(in.rsrp_dbm, out.rsrp_dbm);
}

static void test_layout_header_and_padding(void)
{
    tracker_report_t r = sample();
    uint8_t buf[PAYLOAD_LEN];
    TEST_ASSERT_TRUE(payload_encode(&r, buf));
    // ver=01 type=000 fix=10 moving=1 -> 0100 0101 = 0x45, then seq = 42 = 0x2A
    TEST_ASSERT_EQUAL_HEX8(0x45, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x2A, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[PAYLOAD_LEN - 1U] & 0x0FU);  // 4 padding bits
}

static void test_out_of_range_fields_are_clamped(void)
{
    tracker_report_t r = sample();
    r.alt_m = 99999;
    r.speed_kmh = 400U;
    r.vbat_mv = 1500U;
    r.temp_c = 120;
    r.rsrp_dbm = -150;
    uint8_t buf[PAYLOAD_LEN];
    tracker_report_t out;
    TEST_ASSERT_TRUE(payload_encode(&r, buf));
    TEST_ASSERT_TRUE(payload_decode(buf, &out));
    TEST_ASSERT_EQUAL_INT32(7688, out.alt_m);  // (2047 * 4) - 500
    TEST_ASSERT_EQUAL_UINT16(255U, out.speed_kmh);
    TEST_ASSERT_EQUAL_UINT16(2000U, out.vbat_mv);
    TEST_ASSERT_EQUAL_INT16(87, out.temp_c);
    TEST_ASSERT_EQUAL_INT16(-140, out.rsrp_dbm);
}

static void test_poles_and_antimeridian(void)
{
    tracker_report_t r = sample();
    uint8_t buf[PAYLOAD_LEN];
    tracker_report_t out;
    const int32_t lats[] = {900000000, -900000000, 0};
    const int32_t lons[] = {1800000000, -1800000000, 0};
    for (unsigned i = 0; i < 3U; i++) {
        r.lat_e7 = lats[i];
        r.lon_e7 = lons[i];
        TEST_ASSERT_TRUE(payload_encode(&r, buf));
        TEST_ASSERT_TRUE(payload_decode(buf, &out));
        TEST_ASSERT_INT32_WITHIN(108, r.lat_e7, out.lat_e7);
        TEST_ASSERT_INT32_WITHIN(216, r.lon_e7, out.lon_e7);
    }
}

static void test_invalid_input_rejected(void)
{
    tracker_report_t r = sample();
    uint8_t buf[PAYLOAD_LEN];
    r.unix_time = PAYLOAD_EPOCH_UNIX - 1U;
    TEST_ASSERT_FALSE(payload_encode(&r, buf));
    r = sample();
    r.lat_e7 = 900000001;
    TEST_ASSERT_FALSE(payload_encode(&r, buf));
    r = sample();
    r.lon_e7 = -1800000001;
    TEST_ASSERT_FALSE(payload_encode(&r, buf));
}

static void test_unknown_version_rejected(void)
{
    uint8_t buf[PAYLOAD_LEN] = {0};  // version 0
    tracker_report_t out;
    TEST_ASSERT_FALSE(payload_decode(buf, &out));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_roundtrip_within_quantisation);
    RUN_TEST(test_layout_header_and_padding);
    RUN_TEST(test_out_of_range_fields_are_clamped);
    RUN_TEST(test_poles_and_antimeridian);
    RUN_TEST(test_invalid_input_rejected);
    RUN_TEST(test_unknown_version_rejected);
    return UNITY_END();
}
