#ifndef GNSS_H
#define GNSS_H

#include <stdbool.h>
#include <stdint.h>

// Position from the BG95's built-in GNSS, parsed from the response of AT+QGPSLOC=2:
//   +QGPSLOC: <UTC hhmmss.sss>,<lat>,<lon>,<HDOP>,<alt>,<fix>,<COG ddd.mm>,<spkm>,<spkn>,<date ddmmyy>,<nsat>
// Mode 2 gives latitude/longitude in signed decimal degrees, e.g. 40.41677,-3.70379.
// No floating point: degrees are kept as integers scaled by 1e7, as in the report payload.

typedef struct {
    uint32_t unix_time;
    int32_t lat_e7;
    int32_t lon_e7;
    int32_t alt_m;
    uint16_t hdop_x10;
    uint16_t hacc_m;  // estimate: HDOP x 5 m (typical user range error); not a measured accuracy
    uint8_t fix;      // 2 = 2D, 3 = 3D
    uint16_t heading_deg;
    uint16_t speed_kmh;
    uint8_t nsat;
} gnss_fix_t;

// Returns false if the line is not a complete, plausible +QGPSLOC response (missing fields,
// latitude/longitude out of range, bad date or time, fix not 2 or 3).
bool gnss_parse_qgpsloc(const char *line, gnss_fix_t *out);

// Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's days_from_civil).
int32_t gnss_days_from_civil(int32_t y, uint32_t m, uint32_t d);

#endif
