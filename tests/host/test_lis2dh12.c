// LIS2DH12 driver against a register-level fake: register file, auto-increment, INT1_SRC latch.
#include <string.h>

#include "lis2dh12.h"
#include "unity.h"

typedef struct {
    uint8_t regs[0x40];
    uint8_t write_order[32];  // register addresses in the order they were written
    size_t n_writes;
    bool latched;  // INT1 latched (cleared by reading INT1_SRC)
    bool fail_bus;
    unsigned ref_reads;
} fake_acc_t;

static fake_acc_t acc;

static int fake_read(void *ctx, uint8_t reg, uint8_t *data, size_t len)
{
    fake_acc_t *a = (fake_acc_t *)ctx;
    if (a->fail_bus) {
        return -1;
    }
    for (size_t i = 0; i < len; i++) {
        uint8_t r = (uint8_t)(reg + i);
        if (r == LIS2DH12_INT1_SRC) {
            data[i] = a->latched ? (uint8_t)(LIS2DH12_INT1_SRC_IA | 0x02U) : 0x00U;  // IA | XH
            a->latched = false;
        } else {
            if (r == LIS2DH12_REFERENCE) {
                a->ref_reads++;
            }
            data[i] = a->regs[r & 0x3FU];
        }
    }
    return 0;
}

static int fake_write(void *ctx, uint8_t reg, const uint8_t *data, size_t len)
{
    fake_acc_t *a = (fake_acc_t *)ctx;
    if (a->fail_bus) {
        return -1;
    }
    for (size_t i = 0; i < len; i++) {
        a->regs[(reg + i) & 0x3FU] = data[i];
        if (a->n_writes < sizeof a->write_order) {
            a->write_order[a->n_writes++] = (uint8_t)(reg + i);
        }
    }
    return 0;
}

static const lis2dh12_bus_t bus = {fake_read, fake_write, &acc};

void setUp(void)
{
    memset(&acc, 0, sizeof acc);
    acc.regs[LIS2DH12_WHO_AM_I] = LIS2DH12_WHO_AM_I_VALUE;
}
void tearDown(void) {}

static void test_spi_header_bits(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x8FU, lis2dh12_spi_header(LIS2DH12_WHO_AM_I, true, 1U));
    TEST_ASSERT_EQUAL_HEX8(0xE8U, lis2dh12_spi_header(0x28U, true, 6U));  // OUT_X_L.. with auto-increment
    TEST_ASSERT_EQUAL_HEX8(0x20U, lis2dh12_spi_header(LIS2DH12_CTRL_REG1, false, 1U));
}

static void test_threshold_conversion(void)
{
    TEST_ASSERT_EQUAL_UINT8(5U, lis2dh12_threshold_lsb(80U, LIS2DH12_FS_2G));      // 80 / 16
    TEST_ASSERT_EQUAL_UINT8(3U, lis2dh12_threshold_lsb(80U, LIS2DH12_FS_4G));      // 2.5 -> 3
    TEST_ASSERT_EQUAL_UINT8(1U, lis2dh12_threshold_lsb(0U, LIS2DH12_FS_2G));       // never 0
    TEST_ASSERT_EQUAL_UINT8(127U, lis2dh12_threshold_lsb(4000U, LIS2DH12_FS_2G));  // 7 bits
    TEST_ASSERT_EQUAL_UINT8(5U, lis2dh12_threshold_lsb(1000U, LIS2DH12_FS_16G));   // 5.4 -> 5
}

static void test_duration_conversion(void)
{
    TEST_ASSERT_EQUAL_UINT8(0U, lis2dh12_duration_lsb(0U, LIS2DH12_ODR_10HZ));
    TEST_ASSERT_EQUAL_UINT8(3U, lis2dh12_duration_lsb(300U, LIS2DH12_ODR_10HZ));
    TEST_ASSERT_EQUAL_UINT8(1U, lis2dh12_duration_lsb(1U, LIS2DH12_ODR_10HZ));  // rounded up
    TEST_ASSERT_EQUAL_UINT8(127U, lis2dh12_duration_lsb(60000U, LIS2DH12_ODR_50HZ));
}

static void test_probe(void)
{
    TEST_ASSERT_EQUAL(LIS2DH12_OK, lis2dh12_probe(&bus));
    acc.regs[LIS2DH12_WHO_AM_I] = 0xFFU;  // MISO stuck high / no chip
    TEST_ASSERT_EQUAL(LIS2DH12_ERR_ID, lis2dh12_probe(&bus));
    acc.fail_bus = true;
    TEST_ASSERT_EQUAL(LIS2DH12_ERR_BUS, lis2dh12_probe(&bus));
}

static void test_wake_config_registers(void)
{
    const lis2dh12_wake_cfg_t cfg = {LIS2DH12_ODR_10HZ, LIS2DH12_FS_2G, 80U, 0U};
    TEST_ASSERT_EQUAL(LIS2DH12_OK, lis2dh12_config_wake(&bus, &cfg));
    TEST_ASSERT_EQUAL_HEX8(0x2FU, acc.regs[LIS2DH12_CTRL_REG1]);  // 10 Hz, LPen, XYZ
    TEST_ASSERT_EQUAL_HEX8(0x01U, acc.regs[LIS2DH12_CTRL_REG2]);  // HP filter on INT1
    TEST_ASSERT_EQUAL_HEX8(0x40U, acc.regs[LIS2DH12_CTRL_REG3]);  // IA1 -> INT1
    TEST_ASSERT_EQUAL_HEX8(0x80U, acc.regs[LIS2DH12_CTRL_REG4]);  // BDU, 2 g, not HR
    TEST_ASSERT_EQUAL_HEX8(0x08U, acc.regs[LIS2DH12_CTRL_REG5]);  // latched
    TEST_ASSERT_EQUAL_HEX8(5U, acc.regs[LIS2DH12_INT1_THS]);
    TEST_ASSERT_EQUAL_HEX8(0x2AU, acc.regs[LIS2DH12_INT1_CFG]);  // XH | YH | ZH
}

static void test_reference_read_before_interrupt_enabled(void)
{
    // Enabling INT1_CFG before the filter reference is set fires on gravity at once.
    const lis2dh12_wake_cfg_t cfg = {LIS2DH12_ODR_10HZ, LIS2DH12_FS_2G, 80U, 0U};
    TEST_ASSERT_EQUAL(LIS2DH12_OK, lis2dh12_config_wake(&bus, &cfg));
    TEST_ASSERT_EQUAL_UINT(1U, acc.ref_reads);
    TEST_ASSERT_EQUAL_HEX8(LIS2DH12_INT1_CFG, acc.write_order[acc.n_writes - 1U]);  // last write
}

static void test_ack_releases_latch(void)
{
    bool moved = false;
    acc.latched = true;
    TEST_ASSERT_EQUAL(LIS2DH12_OK, lis2dh12_ack_wake(&bus, &moved));
    TEST_ASSERT_TRUE(moved);
    TEST_ASSERT_EQUAL(LIS2DH12_OK, lis2dh12_ack_wake(&bus, &moved));
    TEST_ASSERT_FALSE(moved);
}

static void test_config_clears_stale_latch(void)
{
    acc.latched = true;  // motion latched before the MCU (re)configured the sensor
    const lis2dh12_wake_cfg_t cfg = {LIS2DH12_ODR_10HZ, LIS2DH12_FS_2G, 80U, 0U};
    TEST_ASSERT_EQUAL(LIS2DH12_OK, lis2dh12_config_wake(&bus, &cfg));
    TEST_ASSERT_FALSE(acc.latched);  // otherwise INT1 stays high and no new edge ever wakes the MCU
}

static void test_bus_error_reported(void)
{
    acc.fail_bus = true;
    const lis2dh12_wake_cfg_t cfg = {LIS2DH12_ODR_10HZ, LIS2DH12_FS_2G, 80U, 0U};
    TEST_ASSERT_EQUAL(LIS2DH12_ERR_BUS, lis2dh12_config_wake(&bus, &cfg));
    bool moved = true;
    TEST_ASSERT_EQUAL(LIS2DH12_ERR_BUS, lis2dh12_ack_wake(&bus, &moved));
    TEST_ASSERT_FALSE(moved);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_spi_header_bits);
    RUN_TEST(test_threshold_conversion);
    RUN_TEST(test_duration_conversion);
    RUN_TEST(test_probe);
    RUN_TEST(test_wake_config_registers);
    RUN_TEST(test_reference_read_before_interrupt_enabled);
    RUN_TEST(test_ack_releases_latch);
    RUN_TEST(test_config_clears_stale_latch);
    RUN_TEST(test_bus_error_reported);
    return UNITY_END();
}
