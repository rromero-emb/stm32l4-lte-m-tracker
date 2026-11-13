#ifndef PSM_H
#define PSM_H

#include <stdint.h>

// LTE Power Saving Mode timers for AT+CPSMS, encoded as the 8-bit strings of 3GPP TS 24.008:
//  - T3412 extended (periodic TAU), "GPRS Timer 3" (10.5.7.4a): bits 8-6 unit, bits 5-1 value
//      000 10 min, 001 1 h, 010 10 h, 011 2 s, 100 30 s, 101 1 min, 110 320 h, 111 deactivated
//  - T3324 (active time), "GPRS Timer 2" (10.5.7.3): 000 2 s, 001 1 min, 010 6 min, 111 deactivated
// The unit codes are not in ascending order (2 s is 011 in Timer 3 but 000 in Timer 2): a
// classic source of a TAU that is 300 times shorter than intended.
//
// Each function picks the smallest representable time >= the request (finest unit on a tie),
// writes the 8 characters + NUL to out, and returns the time actually encoded, in seconds.
// The network may still grant different values: read them back with AT+QPSMS? / +CEREG: 4.

#define PSM_T3412_MAX_S (31UL * 320UL * 3600UL)
#define PSM_T3324_MAX_S (31UL * 360UL)

uint32_t psm_encode_t3412(uint32_t seconds, char out[9]);
uint32_t psm_encode_t3324(uint32_t seconds, char out[9]);

#endif
