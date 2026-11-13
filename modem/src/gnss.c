#include "gnss.h"

#include <stddef.h>
#include <string.h>

int32_t gnss_days_from_civil(int32_t y, uint32_t m, uint32_t d)
{
    y -= (m <= 2U) ? 1 : 0;
    const int32_t era = ((y >= 0) ? y : (y - 399)) / 400;
    const uint32_t yoe = (uint32_t)(y - (era * 400));                                     // [0, 399]
    const uint32_t doy = ((153U * ((m > 2U) ? (m - 3U) : (m + 9U))) + 2U) / 5U + d - 1U;  // [0, 365]
    const uint32_t doe = (yoe * 365U) + (yoe / 4U) - (yoe / 100U) + doy;                  // [0, 146096]
    return (era * 146097) + (int32_t)doe - 719468;
}

// Parses a signed decimal number into an integer scaled by 10^decimals (extra digits are
// truncated). Stops at ',' or the end of the string. Returns NULL on a malformed field.
static const char *parse_fixed(const char *s, unsigned decimals, int64_t *out)
{
    bool neg = false;
    if ((*s == '-') || (*s == '+')) {
        neg = (*s == '-');
        s++;
    }
    int64_t v = 0;
    unsigned int_digits = 0U;
    while ((*s >= '0') && (*s <= '9')) {
        if (int_digits++ > 12U) {
            return NULL;
        }
        v = (v * 10) + (*s++ - '0');
    }
    unsigned frac = 0U;
    if (*s == '.') {
        s++;
        while ((*s >= '0') && (*s <= '9')) {
            if (frac < decimals) {
                v = (v * 10) + (*s - '0');
                frac++;
            }
            s++;
        }
    }
    if ((int_digits == 0U) || ((*s != ',') && (*s != '\0'))) {
        return NULL;
    }
    for (; frac < decimals; frac++) {
        v *= 10;
    }
    *out = neg ? -v : v;
    return s;
}

static bool two_digits(const char *s, uint32_t *out)
{
    if ((s[0] < '0') || (s[0] > '9') || (s[1] < '0') || (s[1] > '9')) {
        return false;
    }
    *out = ((uint32_t)(s[0] - '0') * 10U) + (uint32_t)(s[1] - '0');
    return true;
}

#define N_FIELDS 11U

bool gnss_parse_qgpsloc(const char *line, gnss_fix_t *out)
{
    static const char prefix[] = "+QGPSLOC:";
    if (strncmp(line, prefix, sizeof prefix - 1U) != 0) {
        return false;
    }
    const char *p = line + sizeof prefix - 1U;
    while (*p == ' ') {
        p++;
    }
    // Split into fields (pointers into the line; each ends at ',' or NUL).
    const char *f[N_FIELDS];
    size_t n = 0U;
    f[n++] = p;
    for (; *p != '\0'; p++) {
        if (*p == ',') {
            if (n == N_FIELDS) {
                return false;
            }
            f[n++] = p + 1;
        }
    }
    if (n != N_FIELDS) {
        return false;
    }

    uint32_t hh, mi, ss, dd, mo, yy;
    if (!two_digits(f[0], &hh) || !two_digits(f[0] + 2, &mi) || !two_digits(f[0] + 4, &ss) || !two_digits(f[9], &dd) ||
        !two_digits(f[9] + 2, &mo) || !two_digits(f[9] + 4, &yy)) {
        return false;
    }
    if ((hh > 23U) || (mi > 59U) || (ss > 60U) || (dd < 1U) || (dd > 31U) || (mo < 1U) || (mo > 12U)) {
        return false;
    }

    int64_t lat, lon, hdop, alt, fix, cog, spkm, nsat;
    if ((parse_fixed(f[1], 7U, &lat) == NULL) || (parse_fixed(f[2], 7U, &lon) == NULL) ||
        (parse_fixed(f[3], 1U, &hdop) == NULL) || (parse_fixed(f[4], 0U, &alt) == NULL) ||
        (parse_fixed(f[5], 0U, &fix) == NULL) || (parse_fixed(f[6], 0U, &cog) == NULL) ||
        (parse_fixed(f[7], 1U, &spkm) == NULL) || (parse_fixed(f[10], 0U, &nsat) == NULL)) {
        return false;
    }
    if ((lat < -900000000LL) || (lat > 900000000LL) || (lon < -1800000000LL) || (lon > 1800000000LL) ||
        ((fix != 2) && (fix != 3)) || (hdop < 0) || (hdop > 999) || (cog < 0) || (cog > 359) || (spkm < 0) ||
        (nsat < 0) || (nsat > 255)) {
        return false;
    }

    int32_t days = gnss_days_from_civil(2000 + (int32_t)yy, mo, dd);
    out->unix_time = ((uint32_t)days * 86400U) + (hh * 3600U) + (mi * 60U) + ss;
    out->lat_e7 = (int32_t)lat;
    out->lon_e7 = (int32_t)lon;
    out->alt_m = (int32_t)alt;
    out->hdop_x10 = (uint16_t)hdop;
    out->hacc_m = (uint16_t)(((hdop * 5) + 5) / 10);
    out->fix = (uint8_t)fix;
    out->heading_deg = (uint16_t)cog;  // "ddd.mm": whole degrees are enough for the payload
    out->speed_kmh = (uint16_t)((spkm > 6553) ? 65535 : ((spkm + 5) / 10));
    out->nsat = (uint8_t)nsat;
    return true;
}
