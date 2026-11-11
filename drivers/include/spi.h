#ifndef SPI_H
#define SPI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "stm32l4xx.h"

// Register-level SPI master driver for the STM32L4 SPI (with 32-bit FIFO, RM0351 ch. 42).
// Polled, NSS managed in software (SSM=1, SSI=1). The caller drives its own chip-select GPIO.

typedef enum {
    SPI_OK = 0,
    SPI_ERR_ARG,      // invalid configuration or length
    SPI_ERR_TIMEOUT,  // TXE/RXNE/BSY did not reach the expected state (clock off? wrong pins?)
    SPI_ERR_MODF,     // mode fault: NSS seen low while master (SSM/SSI or NSS wiring problem)
    SPI_ERR_OVR,      // overrun: a received frame was not read in time
} spi_status_t;

typedef struct {
    uint32_t pclk_hz;   // clock of the APB bus the SPI sits on
    uint32_t max_hz;    // highest SCK the slave accepts; the driver picks the fastest BR <= this
    uint8_t mode;       // 0..3 -> CPOL = mode >> 1, CPHA = mode & 1
    uint8_t data_bits;  // 4..16
    bool lsb_first;
} spi_config_t;

#ifndef SPI_TIMEOUT_LOOPS
#define SPI_TIMEOUT_LOOPS 100000U
#endif

// Baud-rate prescaler field BR (0..7, divider 2^(BR+1)) for the fastest SCK <= max_hz.
// If even /256 is too fast, returns 7. *actual_hz (optional) receives the resulting SCK.
uint32_t spi_br_for(uint32_t pclk_hz, uint32_t max_hz, uint32_t *actual_hz);

// Configures CR1/CR2 with SPE=0, then enables the peripheral. The SPI clock must already be on.
spi_status_t spi_init(SPI_TypeDef *spi, const spi_config_t *cfg, uint32_t *actual_hz);

// Full-duplex transfer of `len` frames of up to 8 bits. tx may be NULL (sends 0xFF), rx may be NULL.
spi_status_t spi_transfer8(SPI_TypeDef *spi, const uint8_t *tx, uint8_t *rx, size_t len);

// Same for 9..16-bit frames.
spi_status_t spi_transfer16(SPI_TypeDef *spi, const uint16_t *tx, uint16_t *rx, size_t len);

const char *spi_status_str(spi_status_t st);

#endif
