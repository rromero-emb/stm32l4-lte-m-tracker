// Decoding of the Cortex-M4 fault status registers (ARMv7-M ARM B3.2.15/B3.2.16).
// No hardware access here, so it builds and is unit-tested on the host.
#include <string.h>

#include "fault.h"

typedef struct {
    uint32_t mask;
    const char *name;
} bit_name_t;

static const bit_name_t cfsr_bits[] = {
    // MMFSR (bits 0..7)
    {1UL << 0, "IACCVIOL"},
    {1UL << 1, "DACCVIOL"},
    {1UL << 3, "MUNSTKERR"},
    {1UL << 4, "MSTKERR"},
    {1UL << 5, "MLSPERR"},
    {1UL << 7, "MMARVALID"},
    // BFSR (bits 8..15)
    {1UL << 8, "IBUSERR"},
    {1UL << 9, "PRECISERR"},
    {1UL << 10, "IMPRECISERR"},
    {1UL << 11, "UNSTKERR"},
    {1UL << 12, "STKERR"},
    {1UL << 13, "LSPERR"},
    {1UL << 15, "BFARVALID"},
    // UFSR (bits 16..31)
    {1UL << 16, "UNDEFINSTR"},
    {1UL << 17, "INVSTATE"},
    {1UL << 18, "INVPC"},
    {1UL << 19, "NOCP"},
    {1UL << 24, "UNALIGNED"},
    {1UL << 25, "DIVBYZERO"},
};

static const bit_name_t hfsr_bits[] = {
    {1UL << 1, "VECTTBL"},
    {1UL << 30, "FORCED"},
    {1UL << 31, "DEBUGEVT"},
};

const char *fault_exception_name(uint32_t exception)
{
    switch (exception) {
    case 3U:
        return "HardFault";
    case 4U:
        return "MemManage";
    case 5U:
        return "BusFault";
    case 6U:
        return "UsageFault";
    default:
        return "unknown";
    }
}

static size_t append(char *buf, size_t size, size_t pos, const char *s)
{
    size_t len = strlen(s);
    if (pos + len + 1U > size) {
        return pos;  // does not fit: stop here, buffer stays NUL-terminated
    }
    memcpy(&buf[pos], s, len + 1U);
    return pos + len;
}

static size_t append_bits(char *buf, size_t size, size_t pos, uint32_t value, const bit_name_t *bits, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if ((value & bits[i].mask) != 0U) {
            if (pos > 0U) {
                pos = append(buf, size, pos, ", ");
            }
            pos = append(buf, size, pos, bits[i].name);
        }
    }
    return pos;
}

size_t fault_describe(uint32_t cfsr, uint32_t hfsr, char *buf, size_t size)
{
    if ((buf == NULL) || (size == 0U)) {
        return 0U;
    }
    buf[0] = '\0';
    size_t pos = append_bits(buf, size, 0U, cfsr, cfsr_bits, sizeof cfsr_bits / sizeof cfsr_bits[0]);
    pos = append_bits(buf, size, pos, hfsr, hfsr_bits, sizeof hfsr_bits / sizeof hfsr_bits[0]);
    if (pos == 0U) {
        pos = append(buf, size, 0U, "none");
    }
    return pos;
}
