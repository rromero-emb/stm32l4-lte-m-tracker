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
    return UNITY_END();
}
