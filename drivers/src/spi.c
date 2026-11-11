#include "spi.h"

// DR must be accessed with the frame width: 8-bit accesses for <= 8-bit frames (a 16-bit write
// packs two frames into the TX FIFO), 16-bit for larger frames. Going through uintptr_t makes the
// narrow access explicit for the compiler (no type punning of the struct member).
static inline volatile uint8_t *dr8(SPI_TypeDef *spi)
{
    return (volatile uint8_t *)(uintptr_t)&spi->DR;
}

static inline volatile uint16_t *dr16(SPI_TypeDef *spi)
{
    return (volatile uint16_t *)(uintptr_t)&spi->DR;
}

// Data size in CR2.DS is (bits - 1); 0..2 are "not allowed" (forced to 8 bits by hardware).
static uint32_t data_bits_of(const SPI_TypeDef *spi)
{
    return (uint32_t)((spi->CR2 & SPI_CR2_DS) >> SPI_CR2_DS_Pos) + 1U;
}

static bool wait_flag(const SPI_TypeDef *spi, uint32_t flag, bool set)
{
    for (uint32_t i = 0; i < SPI_TIMEOUT_LOOPS; i++) {
        if (((spi->SR & flag) != 0U) == set) {
            return true;
        }
    }
    return false;
}

static spi_status_t check_errors(SPI_TypeDef *spi)
{
    uint32_t sr = spi->SR;
    if ((sr & SPI_SR_MODF) != 0U) {
        // Cleared by reading SR (done) then writing CR1. MODF also clears MSTR and SPE.
        // cppcheck-suppress selfAssignment ; a register write is the documented way to clear MODF
        spi->CR1 = spi->CR1;
        return SPI_ERR_MODF;
    }
    if ((sr & SPI_SR_OVR) != 0U) {
        // Cleared by reading DR then SR.
        (void)*dr8(spi);
        (void)spi->SR;
        return SPI_ERR_OVR;
    }
    return SPI_OK;
}

// End of transaction (RM0351 42.4.9): wait TX FIFO empty (FTLVL = 0), then BSY = 0.
// Every received frame was already read in the loop, so the RX FIFO is empty here.
static spi_status_t wait_idle(SPI_TypeDef *spi)
{
    for (uint32_t i = 0; (spi->SR & SPI_SR_FTLVL) != 0U; i++) {
        if (i >= SPI_TIMEOUT_LOOPS) {
            return SPI_ERR_TIMEOUT;
        }
    }
    if (!wait_flag(spi, SPI_SR_BSY, false)) {
        return SPI_ERR_TIMEOUT;
    }
    return check_errors(spi);
}

uint32_t spi_br_for(uint32_t pclk_hz, uint32_t max_hz, uint32_t *actual_hz)
{
    uint32_t br = 0U;
    while ((br < 7U) && ((pclk_hz >> (br + 1U)) > max_hz)) {
        br++;
    }
    if (actual_hz != NULL) {
        *actual_hz = pclk_hz >> (br + 1U);
    }
    return br;
}

spi_status_t spi_init(SPI_TypeDef *spi, const spi_config_t *cfg, uint32_t *actual_hz)
{
    if ((spi == NULL) || (cfg == NULL) || (cfg->mode > 3U) || (cfg->data_bits < 4U) || (cfg->data_bits > 16U) ||
        (cfg->max_hz == 0U) || (cfg->pclk_hz == 0U)) {
        return SPI_ERR_ARG;
    }

    // Configuration registers may only be changed with SPE = 0.
    spi->CR1 &= ~(uint32_t)SPI_CR1_SPE;

    uint32_t br = spi_br_for(cfg->pclk_hz, cfg->max_hz, actual_hz);
    uint32_t cr1 = SPI_CR1_MSTR | (br << SPI_CR1_BR_Pos) | SPI_CR1_SSM | SPI_CR1_SSI;
    if ((cfg->mode & 2U) != 0U) {
        cr1 |= SPI_CR1_CPOL;
    }
    if ((cfg->mode & 1U) != 0U) {
        cr1 |= SPI_CR1_CPHA;
    }
    if (cfg->lsb_first) {
        cr1 |= SPI_CR1_LSBFIRST;
    }

    uint32_t cr2 = ((uint32_t)(cfg->data_bits - 1U) << SPI_CR2_DS_Pos);
    if (cfg->data_bits <= 8U) {
        // RXNE must fire at 8 bits of FIFO level for byte frames, or it never comes for a single byte.
        cr2 |= SPI_CR2_FRXTH;
    }

    spi->CR2 = cr2;
    spi->CR1 = cr1;
    // cppcheck-suppress redundantAssignment ; configure with SPE=0 first, then enable (RM0351 42.4.7)
    spi->CR1 = cr1 | SPI_CR1_SPE;
    return SPI_OK;
}

spi_status_t spi_transfer8(SPI_TypeDef *spi, const uint8_t *tx, uint8_t *rx, size_t len)
{
    if ((spi == NULL) || (len == 0U) || (data_bits_of(spi) > 8U)) {
        return SPI_ERR_ARG;
    }
    for (size_t i = 0; i < len; i++) {
        spi_status_t st = check_errors(spi);
        if (st != SPI_OK) {
            return st;
        }
        if (!wait_flag(spi, SPI_SR_TXE, true)) {
            return SPI_ERR_TIMEOUT;
        }
        *dr8(spi) = (tx != NULL) ? tx[i] : 0xFFU;
        if (!wait_flag(spi, SPI_SR_RXNE, true)) {
            return SPI_ERR_TIMEOUT;
        }
        uint8_t b = *dr8(spi);
        if (rx != NULL) {
            rx[i] = b;
        }
    }
    return wait_idle(spi);
}

spi_status_t spi_transfer16(SPI_TypeDef *spi, const uint16_t *tx, uint16_t *rx, size_t len)
{
    if ((spi == NULL) || (len == 0U) || (data_bits_of(spi) <= 8U)) {
        return SPI_ERR_ARG;
    }
    for (size_t i = 0; i < len; i++) {
        spi_status_t st = check_errors(spi);
        if (st != SPI_OK) {
            return st;
        }
        if (!wait_flag(spi, SPI_SR_TXE, true)) {
            return SPI_ERR_TIMEOUT;
        }
        *dr16(spi) = (tx != NULL) ? tx[i] : 0xFFFFU;
        if (!wait_flag(spi, SPI_SR_RXNE, true)) {
            return SPI_ERR_TIMEOUT;
        }
        uint16_t w = *dr16(spi);
        if (rx != NULL) {
            rx[i] = w;
        }
    }
    return wait_idle(spi);
}

const char *spi_status_str(spi_status_t st)
{
    switch (st) {
    case SPI_OK:
        return "OK";
    case SPI_ERR_ARG:
        return "invalid argument";
    case SPI_ERR_TIMEOUT:
        return "timeout";
    case SPI_ERR_MODF:
        return "mode fault (MODF)";
    case SPI_ERR_OVR:
        return "overrun (OVR)";
    default:
        return "?";
    }
}
