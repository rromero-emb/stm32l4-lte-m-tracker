// Host tool used by tests/host/test_payload_vectors.py to check the C codec against the
// shared vectors and the Python reference decoder.
//   payload_cli encode type fix moving seq unix_time lat_e7 lon_e7 alt speed heading hacc nsat ttff vbat temp rsrp
//   payload_cli decode <40 hex chars>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "payload.h"

int main(int argc, char **argv)
{
    if ((argc == 18) && (strcmp(argv[1], "encode") == 0)) {
        long v[16];
        for (int i = 0; i < 16; i++) {
            v[i] = strtol(argv[i + 2], NULL, 10);
        }
        tracker_report_t r = {
            (msg_type_t)v[0], (fix_type_t)v[1], v[2] != 0,       (uint8_t)v[3],   (uint32_t)strtoul(argv[6], NULL, 10),
            (int32_t)v[5],    (int32_t)v[6],    (int32_t)v[7],   (uint16_t)v[8],  (uint16_t)v[9],
            (uint16_t)v[10],  (uint8_t)v[11],   (uint16_t)v[12], (uint16_t)v[13], (int16_t)v[14],
            (int16_t)v[15]};
        uint8_t out[PAYLOAD_LEN];
        if (!payload_encode(&r, out)) {
            return 1;
        }
        for (unsigned i = 0; i < PAYLOAD_LEN; i++) {
            printf("%02x", out[i]);
        }
        printf("\n");
        return 0;
    }
    if ((argc == 3) && (strcmp(argv[1], "decode") == 0) && (strlen(argv[2]) == 2U * PAYLOAD_LEN)) {
        uint8_t in[PAYLOAD_LEN];
        for (unsigned i = 0; i < PAYLOAD_LEN; i++) {
            char byte[3] = {argv[2][2 * i], argv[2][2 * i + 1], '\0'};
            in[i] = (uint8_t)strtoul(byte, NULL, 16);
        }
        tracker_report_t r;
        if (!payload_decode(in, &r)) {
            return 1;
        }
        printf("%d %d %d %u %lu %ld %ld %ld %u %u %u %u %u %u %d %d\n", r.type, r.fix, r.moving, r.seq,
               (unsigned long)r.unix_time, (long)r.lat_e7, (long)r.lon_e7, (long)r.alt_m, r.speed_kmh, r.heading_deg,
               r.hacc_m, r.nsat, r.ttff_s, r.vbat_mv, r.temp_c, r.rsrp_dbm);
        return 0;
    }
    fprintf(stderr, "usage: see source\n");
    return 2;
}
