#ifndef POLICY_H
#define POLICY_H

#include <stdbool.h>
#include <stdint.h>

#include "payload.h"

// When to send a report. Pure logic on an uptime clock in seconds (no RTOS, host-tested).
//  - still: a heartbeat every period_still_s
//  - motion interrupt while still -> MOTION_START report, then PERIODIC every period_moving_s
//  - no motion interrupt for still_after_s -> MOTION_STOP report, back to still
// Event reports are rate-limited by min_gap_s so a shaken device cannot drain the battery.

typedef struct {
    uint32_t period_still_s;
    uint32_t period_moving_s;
    uint32_t still_after_s;
    uint32_t min_gap_s;
} policy_cfg_t;

typedef struct {
    bool moving;
    bool have_reported;
    bool event_pending;
    msg_type_t event;
    uint32_t last_motion_s;
    uint32_t last_report_s;
} policy_t;

void policy_init(policy_t *p);
void policy_on_motion(policy_t *p, uint32_t now_s);
// True if a report is due now; *type is the message type to send.
bool policy_due(policy_t *p, const policy_cfg_t *cfg, uint32_t now_s, msg_type_t *type);
// Call after a report attempt (sent or not: a failed attempt also waits for the next slot).
void policy_reported(policy_t *p, uint32_t now_s);
// Seconds until policy_due() can change its answer without a new motion event (>= 1).
uint32_t policy_next_check_s(const policy_t *p, const policy_cfg_t *cfg, uint32_t now_s);

#endif
