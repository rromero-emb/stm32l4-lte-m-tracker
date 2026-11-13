#include "psm.h"

#include <stddef.h>

typedef struct {
    uint8_t code;
    uint32_t seconds;
} psm_unit_t;

// Sorted by step size.
static const psm_unit_t t3412_units[] = {
    {3U, 2U}, {4U, 30U}, {5U, 60U}, {0U, 600U}, {1U, 3600U}, {2U, 36000U}, {6U, 1152000U},
};
static const psm_unit_t t3324_units[] = {{0U, 2U}, {1U, 60U}, {2U, 360U}};

static uint32_t encode(uint32_t seconds, const psm_unit_t *units, size_t n_units, char out[9])
{
    uint32_t best = UINT32_MAX;
    uint8_t byte = 0U;
    for (size_t i = 0; i < n_units; i++) {
        uint32_t value = (seconds + units[i].seconds - 1U) / units[i].seconds;  // round up
        if (value > 31U) {
            continue;
        }
        uint32_t t = value * units[i].seconds;
        if (t < best) {  // strict: a tie keeps the finer unit found first
            best = t;
            byte = (uint8_t)((units[i].code << 5) | value);
        }
    }
    if (best == UINT32_MAX) {  // above the largest value: use the maximum
        const psm_unit_t *u = &units[n_units - 1U];
        best = 31U * u->seconds;
        byte = (uint8_t)((u->code << 5) | 31U);
    }
    for (unsigned b = 0U; b < 8U; b++) {
        out[b] = ((byte & (0x80U >> b)) != 0U) ? '1' : '0';
    }
    out[8] = '\0';
    return best;
}

uint32_t psm_encode_t3412(uint32_t seconds, char out[9])
{
    return encode(seconds, t3412_units, sizeof t3412_units / sizeof t3412_units[0], out);
}

uint32_t psm_encode_t3324(uint32_t seconds, char out[9])
{
    return encode(seconds, t3324_units, sizeof t3324_units / sizeof t3324_units[0], out);
}
