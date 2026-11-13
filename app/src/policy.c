#include "policy.h"

#include <string.h>

void policy_init(policy_t *p)
{
    memset(p, 0, sizeof *p);
}

void policy_on_motion(policy_t *p, uint32_t now_s)
{
    p->last_motion_s = now_s;
    if (!p->moving) {
        p->moving = true;
        p->event_pending = true;
        p->event = MSG_MOTION_START;
    }
}

static uint32_t since(uint32_t now_s, uint32_t then_s)
{
    return now_s - then_s;  // unsigned: correct across wrap-around
}

bool policy_due(policy_t *p, const policy_cfg_t *cfg, uint32_t now_s, msg_type_t *type)
{
    if (p->moving && (since(now_s, p->last_motion_s) >= cfg->still_after_s)) {
        p->moving = false;
        p->event_pending = true;
        p->event = MSG_MOTION_STOP;
    }
    if (!p->have_reported) {
        *type = p->event_pending ? p->event : MSG_HEARTBEAT;  // first report right after boot
        return true;
    }
    uint32_t gap = since(now_s, p->last_report_s);
    if (p->event_pending && (gap >= cfg->min_gap_s)) {
        *type = p->event;
        return true;
    }
    uint32_t period = p->moving ? cfg->period_moving_s : cfg->period_still_s;
    if (gap >= period) {
        *type = p->moving ? MSG_PERIODIC : MSG_HEARTBEAT;
        return true;
    }
    return false;
}

void policy_reported(policy_t *p, uint32_t now_s)
{
    p->have_reported = true;
    p->event_pending = false;
    p->last_report_s = now_s;
}

static uint32_t remaining(uint32_t elapsed, uint32_t total)
{
    return (elapsed >= total) ? 0U : (total - elapsed);
}

uint32_t policy_next_check_s(const policy_t *p, const policy_cfg_t *cfg, uint32_t now_s)
{
    if (!p->have_reported) {
        return 1U;
    }
    uint32_t gap = since(now_s, p->last_report_s);
    uint32_t next = remaining(gap, p->moving ? cfg->period_moving_s : cfg->period_still_s);
    if (p->event_pending) {
        uint32_t g = remaining(gap, cfg->min_gap_s);
        next = (g < next) ? g : next;
    }
    if (p->moving) {
        uint32_t s = remaining(since(now_s, p->last_motion_s), cfg->still_after_s);
        next = (s < next) ? s : next;
    }
    return (next == 0U) ? 1U : next;
}
