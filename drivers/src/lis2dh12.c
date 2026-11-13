#include "lis2dh12.h"

static const uint16_t mg_per_lsb[] = {16U, 32U, 62U, 186U};
static const uint16_t odr_hz[] = {0U, 1U, 10U, 25U, 50U};

uint8_t lis2dh12_threshold_lsb(uint16_t mg, lis2dh12_fs_t fs)
{
    uint32_t step = mg_per_lsb[(unsigned)fs & 3U];
    uint32_t lsb = ((uint32_t)mg + (step / 2U)) / step;
    if (lsb == 0U) {
        lsb = 1U;
    }
    return (uint8_t)((lsb > 127U) ? 127U : lsb);
}

uint8_t lis2dh12_duration_lsb(uint16_t ms, lis2dh12_odr_t odr)
{
    uint32_t hz = odr_hz[((unsigned)odr <= 4U) ? (unsigned)odr : 0U];
    uint32_t lsb = (((uint32_t)ms * hz) + 999U) / 1000U;
    return (uint8_t)((lsb > 127U) ? 127U : lsb);
}

static lis2dh12_status_t wr(const lis2dh12_bus_t *bus, uint8_t reg, uint8_t value)
{
    return (bus->write(bus->ctx, reg, &value, 1U) == 0) ? LIS2DH12_OK : LIS2DH12_ERR_BUS;
}

static lis2dh12_status_t rd(const lis2dh12_bus_t *bus, uint8_t reg, uint8_t *value)
{
    return (bus->read(bus->ctx, reg, value, 1U) == 0) ? LIS2DH12_OK : LIS2DH12_ERR_BUS;
}

lis2dh12_status_t lis2dh12_probe(const lis2dh12_bus_t *bus)
{
    uint8_t id = 0U;
    lis2dh12_status_t st = rd(bus, LIS2DH12_WHO_AM_I, &id);
    if (st != LIS2DH12_OK) {
        return st;
    }
    // 0x00 or 0xFF usually means no device: MISO floating or stuck, CS not reaching the chip.
    return (id == LIS2DH12_WHO_AM_I_VALUE) ? LIS2DH12_OK : LIS2DH12_ERR_ID;
}

lis2dh12_status_t lis2dh12_config_wake(const lis2dh12_bus_t *bus, const lis2dh12_wake_cfg_t *cfg)
{
    // Sequence from ST AN5005 (LIS2DH12 application note), "wake-up" with the high-pass filter.
    const uint8_t regs[][2] = {
        // ODR, low-power mode (LPen), X/Y/Z enabled
        {LIS2DH12_CTRL_REG1, (uint8_t)(((unsigned)cfg->odr << 4) | 0x08U | 0x07U)},
        // high-pass filter (normal mode, lowest cut-off) applied to the INT1 generator only
        {LIS2DH12_CTRL_REG2, 0x01U},
        // IA1 routed to the INT1 pin
        {LIS2DH12_CTRL_REG3, 0x40U},
        // BDU, full scale; HR = 0 (low-power)
        {LIS2DH12_CTRL_REG4, (uint8_t)(0x80U | (((unsigned)cfg->fs & 3U) << 4))},
        // latch INT1 until INT1_SRC is read: a short shake is not lost while the MCU wakes up
        {LIS2DH12_CTRL_REG5, 0x08U},
        // INT pins active high
        {LIS2DH12_CTRL_REG6, 0x00U},
        {LIS2DH12_INT1_THS, lis2dh12_threshold_lsb(cfg->threshold_mg, cfg->fs)},
        {LIS2DH12_INT1_DURATION, lis2dh12_duration_lsb(cfg->duration_ms, cfg->odr)},
    };
    for (size_t i = 0; i < (sizeof regs / sizeof regs[0]); i++) {
        if (wr(bus, regs[i][0], regs[i][1]) != LIS2DH12_OK) {
            return LIS2DH12_ERR_BUS;
        }
    }
    // Reading REFERENCE sets the high-pass filter to the current acceleration (gravity), so
    // enabling the interrupt below does not fire at once on the 1 g the sensor is sitting in.
    uint8_t dummy = 0U;
    if (rd(bus, LIS2DH12_REFERENCE, &dummy) != LIS2DH12_OK) {
        return LIS2DH12_ERR_BUS;
    }
    // OR of the high events on X, Y and Z
    if (wr(bus, LIS2DH12_INT1_CFG, 0x2AU) != LIS2DH12_OK) {
        return LIS2DH12_ERR_BUS;
    }
    bool ignored = false;
    return lis2dh12_ack_wake(bus, &ignored);  // clear anything latched during setup
}

lis2dh12_status_t lis2dh12_ack_wake(const lis2dh12_bus_t *bus, bool *moved)
{
    uint8_t src = 0U;
    lis2dh12_status_t st = rd(bus, LIS2DH12_INT1_SRC, &src);
    *moved = (st == LIS2DH12_OK) && ((src & LIS2DH12_INT1_SRC_IA) != 0U);
    return st;
}

lis2dh12_status_t lis2dh12_power_down(const lis2dh12_bus_t *bus)
{
    return wr(bus, LIS2DH12_CTRL_REG1, 0x00U);
}
