// SPI driver tests against a register block in host RAM.
// With SR = TXE|RXNE and DR being plain memory, what the driver writes to DR is what it reads
// back: a perfect loopback. Error paths are exercised by setting the SR flags by hand.
#include <string.h>

#include "spi.h"
#include "unity.h"

static SPI_TypeDef fake;

static spi_config_t cfg(uint8_t mode, uint8_t bits, uint32_t max_hz)
{
    spi_config_t c = {.pclk_hz = 80000000U, .max_hz = max_hz, .mode = mode, .data_bits = bits, .lsb_first = false};
    return c;
}

void setUp(void)
{
    memset(&fake, 0, sizeof fake);
    fake.SR = SPI_SR_TXE;  // reset value (RM0351 42.6.3)
}

void tearDown(void) {}

static void test_baud_rate_prescaler(void)
{
    uint32_t hz = 0U;
    TEST_ASSERT_EQUAL_UINT32(0U, spi_br_for(80000000U, 40000000U, &hz));
    TEST_ASSERT_EQUAL_UINT32(40000000U, hz);
    TEST_ASSERT_EQUAL_UINT32(2U, spi_br_for(80000000U, 10000000U, &hz));  // exactly /8
    TEST_ASSERT_EQUAL_UINT32(10000000U, hz);
    TEST_ASSERT_EQUAL_UINT32(6U, spi_br_for(80000000U, 1000000U, &hz));  // /128 = 625 kHz, never above max
    TEST_ASSERT_EQUAL_UINT32(625000U, hz);
    TEST_ASSERT_EQUAL_UINT32(7U, spi_br_for(80000000U, 100U, &hz));  // slowest possible: /256
    TEST_ASSERT_EQUAL_UINT32(312500U, hz);
}

static void test_modes_map_to_cpol_cpha(void)
{
    const uint32_t expected[4] = {0U, SPI_CR1_CPHA, SPI_CR1_CPOL, SPI_CR1_CPOL | SPI_CR1_CPHA};
    for (uint8_t m = 0; m < 4U; m++) {
        spi_config_t c = cfg(m, 8U, 1000000U);
        TEST_ASSERT_EQUAL(SPI_OK, spi_init(&fake, &c, NULL));
        TEST_ASSERT_EQUAL_HEX32(expected[m], fake.CR1 & (SPI_CR1_CPOL | SPI_CR1_CPHA));
    }
}

static void test_master_software_nss_and_enabled(void)
{
    spi_config_t c = cfg(0U, 8U, 1000000U);
    TEST_ASSERT_EQUAL(SPI_OK, spi_init(&fake, &c, NULL));
    // Without SSM+SSI a master with NSS floating gets MODF and drops out of master mode.
    uint32_t must = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI | SPI_CR1_SPE;
    TEST_ASSERT_EQUAL_HEX32(must, fake.CR1 & must);
    TEST_ASSERT_EQUAL_UINT32(6U, (fake.CR1 & SPI_CR1_BR) >> SPI_CR1_BR_Pos);
}

static void test_reinit_rewrites_cr1_completely(void)
{
    fake.CR1 = SPI_CR1_LSBFIRST | SPI_CR1_CPOL | SPI_CR1_SPE;  // left over from a previous user
    spi_config_t c = cfg(0U, 8U, 1000000U);
    TEST_ASSERT_EQUAL(SPI_OK, spi_init(&fake, &c, NULL));
    TEST_ASSERT_EQUAL_HEX32(0U, fake.CR1 & (SPI_CR1_LSBFIRST | SPI_CR1_CPOL));
}

static void test_8bit_frames_set_ds_and_frxth(void)
{
    spi_config_t c = cfg(0U, 8U, 1000000U);
    TEST_ASSERT_EQUAL(SPI_OK, spi_init(&fake, &c, NULL));
    TEST_ASSERT_EQUAL_UINT32(7U, (fake.CR2 & SPI_CR2_DS) >> SPI_CR2_DS_Pos);
    // The classic STM32L4/F7 pitfall: without FRXTH, RXNE waits for 16 bits and a single byte never arrives.
    TEST_ASSERT_TRUE((fake.CR2 & SPI_CR2_FRXTH) != 0U);
}

static void test_12bit_frames_clear_frxth(void)
{
    spi_config_t c = cfg(0U, 12U, 1000000U);
    TEST_ASSERT_EQUAL(SPI_OK, spi_init(&fake, &c, NULL));
    TEST_ASSERT_EQUAL_UINT32(11U, (fake.CR2 & SPI_CR2_DS) >> SPI_CR2_DS_Pos);
    TEST_ASSERT_TRUE((fake.CR2 & SPI_CR2_FRXTH) == 0U);
}

static void test_invalid_config_rejected(void)
{
    spi_config_t c = cfg(4U, 8U, 1000000U);
    TEST_ASSERT_EQUAL(SPI_ERR_ARG, spi_init(&fake, &c, NULL));
    c = cfg(0U, 3U, 1000000U);
    TEST_ASSERT_EQUAL(SPI_ERR_ARG, spi_init(&fake, &c, NULL));
    c = cfg(0U, 17U, 1000000U);
    TEST_ASSERT_EQUAL(SPI_ERR_ARG, spi_init(&fake, &c, NULL));
    c = cfg(0U, 8U, 0U);
    TEST_ASSERT_EQUAL(SPI_ERR_ARG, spi_init(&fake, &c, NULL));
    TEST_ASSERT_EQUAL(SPI_ERR_ARG, spi_init(NULL, &c, NULL));
}

static void init8(void)
{
    spi_config_t c = cfg(0U, 8U, 1000000U);
    TEST_ASSERT_EQUAL(SPI_OK, spi_init(&fake, &c, NULL));
}

static void test_loopback_8bit(void)
{
    init8();
    fake.SR = SPI_SR_TXE | SPI_SR_RXNE;
    const uint8_t tx[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint8_t rx[4] = {0};
    TEST_ASSERT_EQUAL(SPI_OK, spi_transfer8(&fake, tx, rx, sizeof tx));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(tx, rx, sizeof tx);
}

static void test_8bit_frames_use_byte_access_to_dr(void)
{
    init8();
    fake.SR = SPI_SR_TXE | SPI_SR_RXNE;
    fake.DR = 0xA5A5A500U;  // sentinel in the upper bytes
    const uint8_t tx[1] = {0x3C};
    TEST_ASSERT_EQUAL(SPI_OK, spi_transfer8(&fake, tx, NULL, 1U));
    // A 16/32-bit write would have overwritten the sentinel. On the real SPI it would also push
    // a second (garbage) frame into the TX FIFO.
    TEST_ASSERT_EQUAL_HEX32(0xA5A5A53CU, fake.DR);
}

static void test_null_tx_sends_ff(void)
{
    init8();
    fake.SR = SPI_SR_TXE | SPI_SR_RXNE;
    uint8_t rx[2] = {0};
    TEST_ASSERT_EQUAL(SPI_OK, spi_transfer8(&fake, NULL, rx, 2U));
    TEST_ASSERT_EQUAL_HEX8(0xFF, rx[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, rx[1]);
}

static void test_timeout_when_txe_never_set(void)
{
    init8();
    fake.SR = 0U;  // e.g. SPI clock not enabled: registers read as 0
    uint8_t b = 0x55;
    TEST_ASSERT_EQUAL(SPI_ERR_TIMEOUT, spi_transfer8(&fake, &b, NULL, 1U));
}

static void test_timeout_when_rxne_never_set(void)
{
    init8();
    fake.SR = SPI_SR_TXE;  // e.g. FRXTH = 0 with 8-bit frames
    uint8_t b = 0x55;
    TEST_ASSERT_EQUAL(SPI_ERR_TIMEOUT, spi_transfer8(&fake, &b, NULL, 1U));
}

static void test_timeout_when_busy_stuck(void)
{
    init8();
    fake.SR = SPI_SR_TXE | SPI_SR_RXNE | SPI_SR_BSY;
    uint8_t b = 0x55;
    TEST_ASSERT_EQUAL(SPI_ERR_TIMEOUT, spi_transfer8(&fake, &b, NULL, 1U));
}

static void test_mode_fault_reported(void)
{
    init8();
    fake.SR = SPI_SR_TXE | SPI_SR_MODF;
    uint8_t b = 0x55;
    TEST_ASSERT_EQUAL(SPI_ERR_MODF, spi_transfer8(&fake, &b, NULL, 1U));
}

static void test_overrun_reported(void)
{
    init8();
    fake.SR = SPI_SR_TXE | SPI_SR_RXNE | SPI_SR_OVR;
    uint8_t b = 0x55;
    TEST_ASSERT_EQUAL(SPI_ERR_OVR, spi_transfer8(&fake, &b, NULL, 1U));
}

static void test_frame_size_mismatch_rejected(void)
{
    init8();
    uint16_t w = 0x1234;
    TEST_ASSERT_EQUAL(SPI_ERR_ARG, spi_transfer16(&fake, &w, NULL, 1U));
    spi_config_t c = cfg(0U, 16U, 1000000U);
    TEST_ASSERT_EQUAL(SPI_OK, spi_init(&fake, &c, NULL));
    uint8_t b = 0x12;
    TEST_ASSERT_EQUAL(SPI_ERR_ARG, spi_transfer8(&fake, &b, NULL, 1U));
    TEST_ASSERT_EQUAL(SPI_ERR_ARG, spi_transfer8(&fake, &b, NULL, 0U));
}

static void test_loopback_16bit(void)
{
    spi_config_t c = cfg(3U, 16U, 1000000U);
    TEST_ASSERT_EQUAL(SPI_OK, spi_init(&fake, &c, NULL));
    fake.SR = SPI_SR_TXE | SPI_SR_RXNE;
    const uint16_t tx[3] = {0x0001, 0xBEEF, 0xFFFF};
    uint16_t rx[3] = {0};
    TEST_ASSERT_EQUAL(SPI_OK, spi_transfer16(&fake, tx, rx, 3U));
    TEST_ASSERT_EQUAL_HEX16_ARRAY(tx, rx, 3U);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_baud_rate_prescaler);
    RUN_TEST(test_modes_map_to_cpol_cpha);
    RUN_TEST(test_master_software_nss_and_enabled);
    RUN_TEST(test_reinit_rewrites_cr1_completely);
    RUN_TEST(test_8bit_frames_set_ds_and_frxth);
    RUN_TEST(test_12bit_frames_clear_frxth);
    RUN_TEST(test_invalid_config_rejected);
    RUN_TEST(test_loopback_8bit);
    RUN_TEST(test_8bit_frames_use_byte_access_to_dr);
    RUN_TEST(test_null_tx_sends_ff);
    RUN_TEST(test_timeout_when_txe_never_set);
    RUN_TEST(test_timeout_when_rxne_never_set);
    RUN_TEST(test_timeout_when_busy_stuck);
    RUN_TEST(test_mode_fault_reported);
    RUN_TEST(test_overrun_reported);
    RUN_TEST(test_frame_size_mismatch_rejected);
    RUN_TEST(test_loopback_16bit);
    return UNITY_END();
}
