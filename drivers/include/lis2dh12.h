#ifndef LIS2DH12_H
#define LIS2DH12_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ST LIS2DH12 accelerometer, configured as a wake-on-motion sensor: low-power mode, high-pass
// filtered acceleration compared against a threshold on INT1 (latched). The MCU sleeps in Stop 2
// and an EXTI edge on INT1 wakes it. Register map: LIS2DH12 datasheet (DocID025056), section 8.
//
// Bus-independent: the caller provides register read/write over SPI or I2C. Over SPI the first
// byte is RW (bit 7, 1 = read) | MS (bit 6, auto-increment) | address; see lis2dh12_spi_header().

#define LIS2DH12_WHO_AM_I 0x0FU
#define LIS2DH12_WHO_AM_I_VALUE 0x33U
#define LIS2DH12_CTRL_REG1 0x20U
#define LIS2DH12_CTRL_REG2 0x21U
#define LIS2DH12_CTRL_REG3 0x22U
#define LIS2DH12_CTRL_REG4 0x23U
#define LIS2DH12_CTRL_REG5 0x24U
#define LIS2DH12_CTRL_REG6 0x25U
#define LIS2DH12_REFERENCE 0x26U
#define LIS2DH12_INT1_CFG 0x30U
#define LIS2DH12_INT1_SRC 0x31U
#define LIS2DH12_INT1_THS 0x32U
#define LIS2DH12_INT1_DURATION 0x33U

#define LIS2DH12_INT1_SRC_IA 0x40U

typedef struct {
    // Return 0 on success.
    int (*read)(void *ctx, uint8_t reg, uint8_t *data, size_t len);
    int (*write)(void *ctx, uint8_t reg, const uint8_t *data, size_t len);
    void *ctx;
} lis2dh12_bus_t;

typedef enum { LIS2DH12_FS_2G = 0, LIS2DH12_FS_4G, LIS2DH12_FS_8G, LIS2DH12_FS_16G } lis2dh12_fs_t;

typedef enum {
    LIS2DH12_ODR_1HZ = 1,
    LIS2DH12_ODR_10HZ = 2,
    LIS2DH12_ODR_25HZ = 3,
    LIS2DH12_ODR_50HZ = 4,
} lis2dh12_odr_t;

typedef struct {
    lis2dh12_odr_t odr;
    lis2dh12_fs_t fs;
    uint16_t threshold_mg;  // high-pass filtered acceleration on any axis above this -> INT1
    uint16_t duration_ms;   // ... for at least this long (0 = a single sample is enough)
} lis2dh12_wake_cfg_t;

typedef enum { LIS2DH12_OK = 0, LIS2DH12_ERR_BUS, LIS2DH12_ERR_ID } lis2dh12_status_t;

static inline uint8_t lis2dh12_spi_header(uint8_t reg, bool read, size_t len)
{
    return (uint8_t)((read ? 0x80U : 0x00U) | ((len > 1U) ? 0x40U : 0x00U) | (reg & 0x3FU));
}

// INT1_THS value for a threshold. 1 LSB = 16 / 32 / 62 / 186 mg at 2 / 4 / 8 / 16 g (low-power
// and normal mode). Rounded to nearest, at least 1 (0 would trigger on noise), at most 127.
uint8_t lis2dh12_threshold_lsb(uint16_t mg, lis2dh12_fs_t fs);
// INT1_DURATION value: 1 LSB = 1 / ODR. Rounded up, at most 127.
uint8_t lis2dh12_duration_lsb(uint16_t ms, lis2dh12_odr_t odr);

lis2dh12_status_t lis2dh12_probe(const lis2dh12_bus_t *bus);
lis2dh12_status_t lis2dh12_config_wake(const lis2dh12_bus_t *bus, const lis2dh12_wake_cfg_t *cfg);
// Reads INT1_SRC, which also releases the latched interrupt. *moved = IA bit.
lis2dh12_status_t lis2dh12_ack_wake(const lis2dh12_bus_t *bus, bool *moved);
// Power-down mode (ODR = 0): about 0.5 uA.
lis2dh12_status_t lis2dh12_power_down(const lis2dh12_bus_t *bus);

#endif
