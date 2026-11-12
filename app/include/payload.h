#ifndef PAYLOAD_H
#define PAYLOAD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Tracker report, bit-packed MSB-first into PAYLOAD_LEN bytes (156 bits + 4 padding bits).
// Fits an NB-IoT/NTN satellite message as well as a UDP datagram. Format in docs/PAYLOAD.md;
// tools/payload.py is the reference decoder, and tests/vectors/payload.json is shared by both.

#define PAYLOAD_LEN 20U
#define PAYLOAD_VERSION 1U
#define PAYLOAD_EPOCH_UNIX 1735689600U  // 2025-01-01T00:00:00Z

typedef enum {
    MSG_PERIODIC = 0,
    MSG_MOTION_START = 1,
    MSG_MOTION_STOP = 2,
    MSG_HEARTBEAT = 3,
    MSG_ALARM = 4,
} msg_type_t;

typedef enum { FIX_NONE = 0, FIX_2D = 1, FIX_3D = 2, FIX_LAST_KNOWN = 3 } fix_type_t;

typedef struct {
    msg_type_t type;
    fix_type_t fix;
    bool moving;
    uint8_t seq;
    uint32_t unix_time;  // seconds; must be >= PAYLOAD_EPOCH_UNIX
    int32_t lat_e7;      // degrees * 1e7
    int32_t lon_e7;
    int32_t alt_m;
    uint16_t speed_kmh;
    uint16_t heading_deg;
    uint16_t hacc_m;  // horizontal accuracy
    uint8_t nsat;
    uint16_t ttff_s;  // time to fix of this report
    uint16_t vbat_mv;
    int16_t temp_c;
    int16_t rsrp_dbm;  // LTE signal at send time
} tracker_report_t;

// Out-of-range values are clamped to the field's range. Returns false on invalid input
// (time before the epoch, latitude/longitude outside +/-90/+/-180).
bool payload_encode(const tracker_report_t *r, uint8_t out[PAYLOAD_LEN]);
// Decodes to the quantised values (the centre of each step for lat/lon/alt/heading).
bool payload_decode(const uint8_t in[PAYLOAD_LEN], tracker_report_t *r);

#endif
