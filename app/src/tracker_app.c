// App task: decides when to report (policy.c), gets a fix, encodes the 20-byte report, sends
// it and powers the modem off again. Between reports it blocks until the next policy deadline
// or a motion event, so the idle task can put the MCU into Stop 2.
#include "app.h"
#include "payload.h"
#include "pm.h"
#include "policy.h"

#define GNSS_TIMEOUT_MS 90000U

static StaticTask_t tcb;
static StackType_t stack[384];
static TaskHandle_t self;

static const policy_cfg_t policy_cfg = {
    .period_still_s = 3600U,  // heartbeat every hour while parked
    .period_moving_s = 300U,  // every 5 min while moving
    .still_after_s = 180U,    // 3 min without a motion interrupt = stopped
    .min_gap_s = 60U,
};
static policy_t policy;
static uint8_t seq;
static uint32_t reports_ok;
static uint32_t reports_failed;

// Last fix, for FIX_LAST_KNOWN reports, and the clock it gave us.
static bool have_last;
static gnss_fix_t last_fix;
static uint32_t last_fix_uptime_s;

static void report(msg_type_t type)
{
    tracker_report_t r = {0};
    r.type = type;
    r.moving = policy.moving;
    r.seq = seq++;
    r.unix_time = PAYLOAD_EPOCH_UNIX;  // "time unknown" until the first fix

    gnss_fix_t fix;
    uint16_t ttff = 0U;
    if (modem_get_fix(GNSS_TIMEOUT_MS, &fix, &ttff)) {
        have_last = true;
        last_fix = fix;
        last_fix_uptime_s = pm_uptime_s();
        r.fix = (fix.fix == 3U) ? FIX_3D : FIX_2D;
        r.ttff_s = ttff;
    } else if (have_last) {
        fix = last_fix;
        fix.unix_time += pm_uptime_s() - last_fix_uptime_s;
        r.fix = FIX_LAST_KNOWN;
        r.ttff_s = 0xFFFFU;  // clamped: means "timed out"
    } else {
        r.fix = FIX_NONE;
    }
    if (r.fix != FIX_NONE) {
        r.unix_time = fix.unix_time;
        r.lat_e7 = fix.lat_e7;
        r.lon_e7 = fix.lon_e7;
        r.alt_m = fix.alt_m;
        r.speed_kmh = fix.speed_kmh;
        r.heading_deg = fix.heading_deg;
        r.hacc_m = fix.hacc_m;
        r.nsat = fix.nsat;
    }
    // Battery voltage, temperature and RSRP are not measured yet (ADC and AT+QCSQ: session 4).

    uint8_t buf[PAYLOAD_LEN];
    bool ok = payload_encode(&r, buf) && modem_send(buf, sizeof buf);
    modem_power_off();
    if (ok) {
        reports_ok++;
    } else {
        reports_failed++;
    }
    console_print("report #%u type %u fix %u: %s\r\n", r.seq, (unsigned)type, (unsigned)r.fix, ok ? "sent" : "FAILED");
}

static void app_task(void *arg)
{
    (void)arg;
    policy_init(&policy);
    for (;;) {
        msg_type_t type;
        if (policy_due(&policy, &policy_cfg, pm_uptime_s(), &type)) {
            report(type);
            policy_reported(&policy, pm_uptime_s());
        }
        uint32_t wait_s = policy_next_check_s(&policy, &policy_cfg, pm_uptime_s());
        uint32_t ev = 0U;
        (void)xTaskNotifyWaitIndexed(0U, 0U, UINT32_MAX, &ev, pdMS_TO_TICKS(wait_s * 1000U));
        if ((ev & EV_MOTION) != 0U) {
            policy_on_motion(&policy, pm_uptime_s());
        }
        if ((ev & EV_REPORT_NOW) != 0U) {
            report(MSG_HEARTBEAT);
            policy_reported(&policy, pm_uptime_s());
        }
    }
}

TaskHandle_t app_task_handle(void)
{
    return self;
}

void app_status(void)
{
    console_print("app: %s, reports %u sent / %u failed, next check in %u s\r\n", policy.moving ? "moving" : "still",
                  (unsigned)reports_ok, (unsigned)reports_failed,
                  (unsigned)policy_next_check_s(&policy, &policy_cfg, pm_uptime_s()));
}

void app_task_start(void)
{
    self = xTaskCreateStatic(app_task, "app", sizeof stack / sizeof stack[0], NULL, PRIO_APP, stack, &tcb);
}
