// BG95 state machine against the scripted fake modem, with simulated time (10 ms steps).
#include <string.h>

#include "bg95.h"
#include "fake_modem.h"
#include "unity.h"

static fake_modem_t fake;
static bg95_t modem;
static const bg95_config_t cfg = {.apn = "iot.example", .host = "203.0.113.10", .port = 5683U};

#define OK "\r\nOK\r\n"

static const fake_step_t bring_up[] = {
    {"ATE0", OK},
    {"AT+CMEE=1", OK},
    {"AT+QCFG=\"iotopmode\",0,1", OK},
    {"AT+CEREG=1", OK},
    {"AT+CFUN=1", OK},
    {"AT+CEREG?", "\r\n+CEREG: 1,2\r\n" OK},  // searching
    {"AT+CEREG?", "\r\n+CEREG: 1,5\r\n" OK},  // registered, roaming
    {"AT+QICSGP=1,1,\"iot.example\",\"\",\"\",0", OK},
    {"AT+QIACT=1", OK},
};

static void start(const fake_step_t *script, size_t n)
{
    fake_now = 0U;
    fake_init(&fake, script, n);
    at_io_t io = {fake_write, &fake};
    bg95_hw_t hw = {fake_pwrkey, &fake};
    bg95_init(&modem, io, hw, &cfg);
    bg95_start(&modem, fake_now);
}

// Advances simulated time until `until` state (or the time limit). Delivers modem output
// each tick. If `rdy_after_pulse`, the fake prints "APP RDY" 2 s after each PWRKEY pulse.
static bool run_until(bg95_state_t until, uint32_t limit_ms, bool rdy_after_pulse)
{
    unsigned pulses_seen = 0U;
    uint32_t rdy_at = 0U;
    for (uint32_t t = 0; t < limit_ms; t += 10U) {
        fake_now += 10U;
        if (fake.pwrkey_pulses != pulses_seen) {
            pulses_seen = fake.pwrkey_pulses;
            rdy_at = fake_now + 2000U;
        }
        if (rdy_after_pulse && (rdy_at != 0U) && (fake_now >= rdy_at)) {
            fake_inject(&fake, "\r\nAPP RDY\r\n");
            rdy_at = 0U;
        }
        uint8_t buf[64];
        size_t n;
        while ((n = fake_take(&fake, buf, sizeof buf)) > 0U) {
            bg95_feed(&modem, buf, n);
        }
        bg95_poll(&modem, fake_now);
        if (bg95_state(&modem) == until) {
            return true;
        }
    }
    return false;
}

void setUp(void) {}
void tearDown(void) {}

static void test_power_on_pulse_and_bring_up(void)
{
    start(bring_up, sizeof bring_up / sizeof bring_up[0]);
    TEST_ASSERT_TRUE_MESSAGE(run_until(BG95_READY, 60000U, true), bg95_state_name(bg95_state(&modem)));
    TEST_ASSERT_EQUAL_UINT(1U, fake.pwrkey_pulses);
    // BG95 hardware design: PWRKEY low for 500..1000 ms
    TEST_ASSERT_UINT32_WITHIN(250U, 750U, fake.pwrkey_last_pulse_ms);
    TEST_ASSERT_TRUE(fake_script_done(&fake));
    TEST_ASSERT_EQUAL_UINT(0U, fake.unexpected);
}

static const fake_step_t bring_up_and_send[] = {
    {"ATE0", OK},
    {"AT+CMEE=1", OK},
    {"AT+QCFG=\"iotopmode\",0,1", OK},
    {"AT+CEREG=1", OK},
    {"AT+CFUN=1", OK},
    {"AT+CEREG?", "\r\n+CEREG: 1,1\r\n" OK},
    {"AT+QICSGP=1,1,\"iot.example\",\"\",\"\",0", OK},
    {"AT+QIACT=1", OK},
    {"AT+QIOPEN=1,0,\"UDP\",\"203.0.113.10\",5683,0,0", OK "\r\n+QIOPEN: 0,0\r\n"},
    {"AT+QISEND=0,5", "\r\n> "},
    {"hello", "\r\nSEND OK\r\n"},
    {"AT+QICLOSE=0", OK},
};

static void test_udp_send(void)
{
    start(bring_up_and_send, sizeof bring_up_and_send / sizeof bring_up_and_send[0]);
    TEST_ASSERT_TRUE(run_until(BG95_READY, 60000U, true));
    TEST_ASSERT_FALSE(bg95_send(&modem, (const uint8_t *)"", 0U));
    TEST_ASSERT_TRUE(bg95_send(&modem, (const uint8_t *)"hello", 5U));
    TEST_ASSERT_EQUAL(BG95_SEND_BUSY, bg95_send_status(&modem));
    TEST_ASSERT_TRUE(run_until(BG95_READY, 10000U, true));
    TEST_ASSERT_EQUAL(BG95_SEND_DONE, bg95_send_status(&modem));
    TEST_ASSERT_TRUE(fake_script_done(&fake));
    TEST_ASSERT_EQUAL_UINT(0U, fake.unexpected);
}

static const fake_step_t socket_error[] = {
    {"ATE0", OK},
    {"AT+CMEE=1", OK},
    {"AT+QCFG=\"iotopmode\",0,1", OK},
    {"AT+CEREG=1", OK},
    {"AT+CFUN=1", OK},
    {"AT+CEREG?", "\r\n+CEREG: 1,1\r\n" OK},
    {"AT+QICSGP=1,1,\"iot.example\",\"\",\"\",0", OK},
    {"AT+QIACT=1", OK},
    {"AT+QIOPEN=1,0,\"UDP\",\"203.0.113.10\",5683,0,0", OK "\r\n+QIOPEN: 0,563\r\n"},  // 563: socket in use
    {"AT+CFUN=0", OK},                                                                 // level-1 recovery
    {"AT+CFUN=1", OK},
};

static void test_socket_open_error_fails_send_and_recovers_radio(void)
{
    start(socket_error, sizeof socket_error / sizeof socket_error[0]);
    TEST_ASSERT_TRUE(run_until(BG95_READY, 60000U, true));
    TEST_ASSERT_TRUE(bg95_send(&modem, (const uint8_t *)"x", 1U));
    TEST_ASSERT_TRUE(run_until(BG95_REGISTER, 10000U, true));
    TEST_ASSERT_EQUAL(BG95_SEND_FAILED, bg95_send_status(&modem));
    TEST_ASSERT_EQUAL(BG95_SENDING, modem.failed_in);
    TEST_ASSERT_TRUE(fake_script_done(&fake));
}

static const fake_step_t cme_in_init[] = {
    {"ATE0", OK},
    {"AT+CMEE=1", OK},
    {"AT+QCFG=\"iotopmode\",0,1", "\r\n+CME ERROR: 4\r\n"},
    {"AT+CFUN=0", OK},
    {"AT+CFUN=1", OK},
    {"AT+CEREG?", "\r\n+CEREG: 1,1\r\n" OK},
    {"AT+QICSGP=1,1,\"iot.example\",\"\",\"\",0", OK},
    {"AT+QIACT=1", OK},
};

static void test_error_during_init_recovers(void)
{
    start(cme_in_init, sizeof cme_in_init / sizeof cme_in_init[0]);
    TEST_ASSERT_TRUE(run_until(BG95_READY, 60000U, true));
    TEST_ASSERT_EQUAL(BG95_INIT, modem.failed_in);
    TEST_ASSERT_EQUAL_UINT8(0U, modem.recoveries);  // reset once fully up
    TEST_ASSERT_TRUE(fake_script_done(&fake));
}

static void test_dead_module_escalates_to_power_cycles_then_fails(void)
{
    start(NULL, 0U);  // never answers, never prints APP RDY
    TEST_ASSERT_TRUE(run_until(BG95_FAILED, 300000U, false));
    // initial power-on + one power cycle per level-2 recovery
    TEST_ASSERT_EQUAL_UINT(3U, fake.pwrkey_pulses);
    TEST_ASSERT_EQUAL(BG95_WAIT_RDY, modem.failed_in);
}

static const fake_step_t reboot_script[] = {
    {"ATE0", OK},
    {"AT+CMEE=1", OK},
    {"AT+QCFG=\"iotopmode\",0,1", OK},
    {"AT+CEREG=1", OK},
    {"AT+CFUN=1", OK},
    {"AT+CEREG?", "\r\n+CEREG: 1,1\r\n" OK},
    {"AT+QICSGP=1,1,\"iot.example\",\"\",\"\",0", OK},
    {"AT+QIACT=1", OK},
    // after the unexpected restart the whole init runs again
    {"ATE0", OK},
    {"AT+CMEE=1", OK},
    {"AT+QCFG=\"iotopmode\",0,1", OK},
    {"AT+CEREG=1", OK},
    {"AT+CFUN=1", OK},
    {"AT+CEREG?", "\r\n+CEREG: 1,1\r\n" OK},
    {"AT+QICSGP=1,1,\"iot.example\",\"\",\"\",0", OK},
    {"AT+QIACT=1", OK},
};

static void test_unexpected_restart_reruns_init(void)
{
    start(reboot_script, sizeof reboot_script / sizeof reboot_script[0]);
    TEST_ASSERT_TRUE(run_until(BG95_READY, 60000U, true));
    fake_inject(&fake, "\r\nAPP RDY\r\n");  // module restarted on its own
    TEST_ASSERT_TRUE(run_until(BG95_INIT, 1000U, false));
    TEST_ASSERT_TRUE(run_until(BG95_READY, 60000U, false));
    TEST_ASSERT_TRUE(fake_script_done(&fake));
    TEST_ASSERT_EQUAL_UINT(1U, fake.pwrkey_pulses);  // no power cycle needed
}

static const fake_step_t never_registers[] = {
    {"ATE0", OK}, {"AT+CMEE=1", OK}, {"AT+QCFG=\"iotopmode\",0,1", OK}, {"AT+CEREG=1", OK}, {"AT+CFUN=1", OK},
};

static void test_registration_timeout_goes_to_recovery(void)
{
    start(never_registers, sizeof never_registers / sizeof never_registers[0]);
    TEST_ASSERT_TRUE(run_until(BG95_REGISTER, 60000U, true));
    // AT+CEREG? is never answered (each poll times out): after BG95_REG_TIMEOUT_MS -> RECOVERY
    TEST_ASSERT_TRUE(run_until(BG95_RECOVERY, BG95_REG_TIMEOUT_MS + 5000U, false));
    TEST_ASSERT_EQUAL(BG95_REGISTER, modem.failed_in);
}

// ---------------------------------------------------------------- session 3: PSM, GNSS, power off

#define READY_STEPS                                                                             \
    {"AT+CEREG?", "\r\n+CEREG: 1,1\r\n" OK}, {"AT+QICSGP=1,1,\"iot.example\",\"\",\"\",0", OK}, \
    {                                                                                           \
        "AT+QIACT=1", OK                                                                        \
    }
#define INIT_STEPS                                                                          \
    {"ATE0", OK}, {"AT+CMEE=1", OK}, {"AT+QCFG=\"iotopmode\",0,1", OK}, {"AT+CEREG=1", OK}, \
    {                                                                                       \
        "AT+CFUN=1", OK                                                                     \
    }

static void start_cfg(const fake_step_t *script, size_t n, const bg95_config_t *c)
{
    fake_now = 0U;
    fake_init(&fake, script, n);
    at_io_t io = {fake_write, &fake};
    bg95_hw_t hw = {fake_pwrkey, &fake};
    bg95_init(&modem, io, hw, c);
    bg95_start(&modem, fake_now);
}

static const fake_step_t with_psm[] = {
    INIT_STEPS,
    {"AT+CPSMS=1,,,\"00111000\",\"00011110\"", OK},  // TAU 24 h, active time 60 s
    READY_STEPS,
};

static void test_psm_requested_in_init(void)
{
    const bg95_config_t c = {.apn = "iot.example",
                             .host = "203.0.113.10",
                             .port = 5683U,
                             .psm = true,
                             .psm_tau_s = 86400U,
                             .psm_active_s = 60U};
    start_cfg(with_psm, sizeof with_psm / sizeof with_psm[0], &c);
    TEST_ASSERT_TRUE(run_until(BG95_READY, 60000U, true));
    TEST_ASSERT_TRUE(fake_script_done(&fake));
    TEST_ASSERT_EQUAL_UINT(0U, fake.unexpected);
}

#define NOT_FIXED "\r\n+CME ERROR: 516\r\n"

static const fake_step_t gnss_fix[] = {
    INIT_STEPS,
    READY_STEPS,
    {"AT+QGPSCFG=\"priority\",0", OK},
    {"AT+QGPS=1", OK},
    {"AT+QGPSLOC=2", NOT_FIXED},
    {"AT+QGPSLOC=2", NOT_FIXED},
    {"AT+QGPSLOC=2", "\r\n+QGPSLOC: 112000.000,40.41677,-3.70379,1.2,657.0,3,271.30,48.6,26.2,121125,09\r\n" OK},
    {"AT+QGPSEND", OK},
    {"AT+QGPSCFG=\"priority\",1", OK},
};

static void test_gnss_fix_after_polling(void)
{
    start(gnss_fix, sizeof gnss_fix / sizeof gnss_fix[0]);
    TEST_ASSERT_TRUE(run_until(BG95_READY, 60000U, true));
    TEST_ASSERT_FALSE(bg95_send(&modem, (const uint8_t *)"", 0U));
    TEST_ASSERT_TRUE(bg95_gnss_start(&modem, 90000U));
    TEST_ASSERT_FALSE(bg95_gnss_start(&modem, 90000U));  // one at a time
    TEST_ASSERT_EQUAL(BG95_GNSS_BUSY, bg95_gnss_status(&modem));
    // false: run_until() would take the earlier PWRKEY pulse as new and fake an "APP RDY" restart
    TEST_ASSERT_TRUE_MESSAGE(run_until(BG95_READY, 20000U, false), fake.last_cmd);
    TEST_ASSERT_EQUAL(BG95_GNSS_FIX, bg95_gnss_status(&modem));
    TEST_ASSERT_EQUAL_INT32(404167700, bg95_gnss_fix(&modem)->lat_e7);
    TEST_ASSERT_EQUAL_UINT32(1762946400U, bg95_gnss_fix(&modem)->unix_time);
    TEST_ASSERT_UINT_WITHIN(1U, 2U, modem.ttff_s);  // two "not fixed" polls, 1 s apart
    TEST_ASSERT_TRUE(fake_script_done(&fake));
    TEST_ASSERT_EQUAL_UINT(0U, fake.unexpected);
}

static const fake_step_t gnss_no_fix[] = {
    INIT_STEPS,
    READY_STEPS,
    {"AT+QGPSCFG=\"priority\",0", OK},
    {"AT+QGPS=1", "\r\n+CME ERROR: 504\r\n"},  // session already running: carry on
    {"AT+QGPSLOC=2", NOT_FIXED},
    {"AT+QGPSLOC=2", NOT_FIXED},
    {"AT+QGPSLOC=2", NOT_FIXED},
    {"AT+QGPSEND", OK},
    {"AT+QGPSCFG=\"priority\",1", OK},
};

static void test_gnss_timeout_stops_engine_and_returns_rf_to_lte(void)
{
    start(gnss_no_fix, sizeof gnss_no_fix / sizeof gnss_no_fix[0]);
    TEST_ASSERT_TRUE(run_until(BG95_READY, 60000U, true));
    TEST_ASSERT_TRUE(bg95_gnss_start(&modem, 2500U));
    TEST_ASSERT_TRUE(run_until(BG95_READY, 20000U, false));
    TEST_ASSERT_EQUAL(BG95_GNSS_NO_FIX, bg95_gnss_status(&modem));
    TEST_ASSERT_TRUE(fake_script_done(&fake));
    TEST_ASSERT_EQUAL_UINT(0U, fake.unexpected);
    TEST_ASSERT_EQUAL_UINT8(0U, modem.recoveries);  // no fix is not a modem failure
}

static const fake_step_t gnss_module_dies[] = {
    INIT_STEPS, READY_STEPS, {"AT+QGPSCFG=\"priority\",0", OK}, {"AT+QGPS=1", OK},
    // AT+QGPSLOC=2 is never answered
};

static void test_gnss_at_timeout_is_a_modem_failure(void)
{
    start(gnss_module_dies, sizeof gnss_module_dies / sizeof gnss_module_dies[0]);
    TEST_ASSERT_TRUE(run_until(BG95_READY, 60000U, true));
    TEST_ASSERT_TRUE(bg95_gnss_start(&modem, 90000U));
    TEST_ASSERT_TRUE(run_until(BG95_RECOVERY, 10000U, false));
    TEST_ASSERT_EQUAL(BG95_GNSS_FAILED, bg95_gnss_status(&modem));
    TEST_ASSERT_EQUAL(BG95_GNSS, modem.failed_in);
}

static const fake_step_t power_off[] = {
    INIT_STEPS,
    READY_STEPS,
    {"AT+QPOWD=1", OK "\r\nPOWERED DOWN\r\n"},
};

static void test_power_off_waits_for_powered_down(void)
{
    start(power_off, sizeof power_off / sizeof power_off[0]);
    TEST_ASSERT_TRUE(run_until(BG95_READY, 60000U, true));
    TEST_ASSERT_TRUE(bg95_power_off(&modem));
    TEST_ASSERT_FALSE(bg95_is_idle(&modem));
    uint32_t t0 = fake_now;
    TEST_ASSERT_TRUE(run_until(BG95_OFF, 10000U, false));
    TEST_ASSERT_LESS_THAN_UINT32(1000U, fake_now - t0);  // did not sit out the timeout
    TEST_ASSERT_TRUE(bg95_is_idle(&modem));
    TEST_ASSERT_FALSE(bg95_power_off(&modem));
    TEST_ASSERT_TRUE(fake_script_done(&fake));
}

static const fake_step_t power_off_silent[] = {
    INIT_STEPS, READY_STEPS, {"AT+QPOWD=1", OK},  // no "POWERED DOWN"
};

static void test_power_off_without_urc_times_out_to_off(void)
{
    start(power_off_silent, sizeof power_off_silent / sizeof power_off_silent[0]);
    TEST_ASSERT_TRUE(run_until(BG95_READY, 60000U, true));
    TEST_ASSERT_TRUE(bg95_power_off(&modem));
    uint32_t t0 = fake_now;
    TEST_ASSERT_TRUE(run_until(BG95_OFF, 10000U, false));
    TEST_ASSERT_UINT32_WITHIN(100U, BG95_POWER_OFF_TIMEOUT_MS, fake_now - t0);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_power_on_pulse_and_bring_up);
    RUN_TEST(test_udp_send);
    RUN_TEST(test_socket_open_error_fails_send_and_recovers_radio);
    RUN_TEST(test_error_during_init_recovers);
    RUN_TEST(test_dead_module_escalates_to_power_cycles_then_fails);
    RUN_TEST(test_unexpected_restart_reruns_init);
    RUN_TEST(test_registration_timeout_goes_to_recovery);
    RUN_TEST(test_psm_requested_in_init);
    RUN_TEST(test_gnss_fix_after_polling);
    RUN_TEST(test_gnss_timeout_stops_engine_and_returns_rf_to_lte);
    RUN_TEST(test_gnss_at_timeout_is_a_modem_failure);
    RUN_TEST(test_power_off_waits_for_powered_down);
    RUN_TEST(test_power_off_without_urc_times_out_to_off);
    return UNITY_END();
}
