#include "bg95.h"

#include <string.h>

#include "fmt.h"

// ---------------------------------------------------------------- URCs

static void urc_rdy(void *ctx, const char *line)
{
    bg95_t *m = (bg95_t *)ctx;
    (void)line;
    if ((m->state >= BG95_INIT) && (m->state <= BG95_SENDING)) {
        m->rebooted = true;  // the module restarted on its own (brown-out, internal watchdog)
    } else {
        m->rdy = true;
    }
}

static void urc_cereg(void *ctx, const char *line)
{
    // With AT+CEREG=1 the URC is "+CEREG: <stat>".
    bg95_t *m = (bg95_t *)ctx;
    int32_t v[1];
    if (at_parse_ints(line, v, 1U) == 1U) {
        m->reg_stat = (int)v[0];
    }
}

static void urc_qiopen(void *ctx, const char *line)
{
    // "+QIOPEN: <connectID>,<err>" arrives after the OK of AT+QIOPEN.
    bg95_t *m = (bg95_t *)ctx;
    int32_t v[2] = {0, -1};
    if (at_parse_ints(line, v, 2U) == 2U) {
        m->socket_open_urc = true;
        m->socket_open_err = (int)v[1];
    }
}

static void urc_ignore(void *ctx, const char *line)
{
    (void)ctx;
    (void)line;
}

// BG95: "APP RDY" means the AT interface is fully usable; a bare "RDY" (older firmware) is not enough.
static const at_urc_t urcs[] = {
    {"APP RDY", urc_rdy},    {"RDY", urc_ignore},    {"+CEREG:", urc_cereg},       {"+QIOPEN:", urc_qiopen},
    {"+QIURC:", urc_ignore}, {"+CPIN:", urc_ignore}, {"POWERED DOWN", urc_ignore}, {"+QIND:", urc_ignore},
};

// ---------------------------------------------------------------- helpers

static void enter(bg95_t *m, bg95_state_t s)
{
    m->state = s;
    m->state_since_ms = m->now_ms;
    m->step = 0U;
    m->cmd_active = false;
    m->next_ms = m->now_ms;
}

static uint32_t in_state_ms(const bg95_t *m)
{
    return m->now_ms - m->state_since_ms;
}

static void fail(bg95_t *m)
{
    m->failed_in = m->state;
    if (m->state == BG95_SENDING) {
        m->send_status = BG95_SEND_FAILED;
    }
    if (m->recoveries >= BG95_MAX_RECOVERIES) {
        enter(m, BG95_FAILED);
        return;
    }
    m->recoveries++;
    enter(m, BG95_RECOVERY);
}

typedef enum { STEP_WAIT, STEP_DONE, STEP_FAILED } step_t;

// Sends `cmd` once, then reports the outcome. Leaves the engine free when done.
static step_t run(bg95_t *m, const char *cmd, const char *expect, uint32_t timeout_ms)
{
    if (!m->cmd_active) {
        if (!at_send(&m->at, cmd, expect, false, timeout_ms, m->now_ms)) {
            return STEP_WAIT;
        }
        m->cmd_active = true;
        return STEP_WAIT;
    }
    at_result_t r = at_result(&m->at);
    if (r == AT_PENDING) {
        return STEP_WAIT;
    }
    m->cmd_active = false;
    return (r == AT_OK) ? STEP_DONE : STEP_FAILED;
}

// ---------------------------------------------------------------- states

static void st_power_on(bg95_t *m)
{
    if (m->step == 0U) {
        m->rdy = false;
        m->hw.pwrkey(m->hw.hw, true);
        m->step = 1U;
    } else if (in_state_ms(m) >= BG95_PWRKEY_PULSE_MS) {
        m->hw.pwrkey(m->hw.hw, false);
        enter(m, BG95_WAIT_RDY);
    } else {
        // keep PWRKEY asserted
    }
}

static void st_wait_rdy(bg95_t *m)
{
    if (m->rdy) {
        enter(m, BG95_INIT);
    } else if (in_state_ms(m) >= BG95_RDY_TIMEOUT_MS) {
        fail(m);
    } else {
        // waiting for "APP RDY"
    }
}

static const char *const init_script[] = {
    "ATE0",                       // no echo
    "AT+CMEE=1",                  // numeric +CME ERROR codes
    "AT+QCFG=\"iotopmode\",0,1",  // LTE-M only, apply now
    "AT+CEREG=1",                 // registration URCs
    "AT+CFUN=1",                  // full functionality
};

static void st_init(bg95_t *m)
{
    if (m->step >= (sizeof init_script / sizeof init_script[0])) {
        m->reg_stat = 0;
        enter(m, BG95_REGISTER);
        return;
    }
    step_t s = run(m, init_script[m->step], NULL, BG95_CMD_TIMEOUT_MS);
    if (s == STEP_DONE) {
        m->step++;
    } else if (s == STEP_FAILED) {
        fail(m);
    } else {
        // in flight
    }
}

static void st_register(bg95_t *m)
{
    if ((m->reg_stat == 1) || (m->reg_stat == 5)) {
        enter(m, BG95_ATTACH);
        return;
    }
    if (in_state_ms(m) >= BG95_REG_TIMEOUT_MS) {
        fail(m);
        return;
    }
    if (!m->cmd_active && ((int32_t)(m->now_ms - m->next_ms) < 0)) {
        return;  // not time to poll yet
    }
    step_t s = run(m, "AT+CEREG?", "+CEREG:", BG95_CMD_TIMEOUT_MS);
    if (s == STEP_DONE) {
        int32_t v[2] = {0, 0};  // "+CEREG: <n>,<stat>"
        if ((at_response(&m->at) != NULL) && (at_parse_ints(at_response(&m->at), v, 2U) == 2U)) {
            m->reg_stat = (int)v[1];
        }
        m->next_ms = m->now_ms + BG95_REG_POLL_MS;
    } else if (s == STEP_FAILED) {
        m->next_ms = m->now_ms + BG95_REG_POLL_MS;  // transient: keep polling until the timeout
    } else {
        // in flight
    }
}

static void st_attach(bg95_t *m)
{
    step_t s = STEP_WAIT;
    if (m->step == 0U) {
        char cmd[96];
        (void)fmt_snprintf(cmd, sizeof cmd, "AT+QICSGP=1,1,\"%s\",\"\",\"\",0", m->cfg.apn);
        s = run(m, cmd, NULL, BG95_CMD_TIMEOUT_MS);
    } else {
        s = run(m, "AT+QIACT=1", NULL, BG95_ATTACH_TIMEOUT_MS);
    }
    if (s == STEP_DONE) {
        if (++m->step == 2U) {
            m->recoveries = 0U;  // fully up again: reset the escalation
            enter(m, BG95_READY);
        }
    } else if (s == STEP_FAILED) {
        fail(m);
    } else {
        // in flight
    }
}

static void st_sending(bg95_t *m)
{
    char cmd[96];
    switch (m->step) {
    case 0: {  // open UDP socket 0 in buffer access mode
        (void)fmt_snprintf(cmd, sizeof cmd, "AT+QIOPEN=1,0,\"UDP\",\"%s\",%u,0,0", m->cfg.host, (unsigned)m->cfg.port);
        if (!m->cmd_active) {
            // Clear only when the command goes out: the "+QIOPEN:" URC often arrives in the
            // same UART read as the OK, i.e. before the next poll.
            m->socket_open_urc = false;
        }
        step_t s = run(m, cmd, NULL, BG95_CMD_TIMEOUT_MS);
        if (s == STEP_DONE) {
            m->step = 1U;
        } else if (s == STEP_FAILED) {
            fail(m);
        } else {
            // in flight
        }
        break;
    }
    case 1:  // wait for "+QIOPEN: 0,<err>"
        if (m->socket_open_urc) {
            if (m->socket_open_err == 0) {
                m->step = 2U;
            } else {
                fail(m);
            }
        } else if (in_state_ms(m) >= BG95_OPEN_TIMEOUT_MS) {
            fail(m);
        } else {
            // waiting
        }
        break;
    case 2:  // AT+QISEND=0,<len> -> '>'
        if (!m->cmd_active) {
            (void)fmt_snprintf(cmd, sizeof cmd, "AT+QISEND=0,%u", (unsigned)m->payload_len);
            if (at_send(&m->at, cmd, NULL, true, BG95_CMD_TIMEOUT_MS, m->now_ms)) {
                m->cmd_active = true;
            }
        } else if (at_result(&m->at) == AT_PROMPT) {
            at_write_raw(&m->at, m->payload, m->payload_len, BG95_CMD_TIMEOUT_MS, m->now_ms);
            m->step = 3U;
        } else if (at_result(&m->at) != AT_PENDING) {
            m->cmd_active = false;
            fail(m);
        } else {
            // waiting for the prompt
        }
        break;
    case 3:  // "SEND OK"
        if (at_result(&m->at) == AT_OK) {
            m->cmd_active = false;
            m->step = 4U;
        } else if (at_result(&m->at) != AT_PENDING) {
            m->cmd_active = false;
            fail(m);
        } else {
            // waiting
        }
        break;
    default: {  // close; the datagram is already out, so a close error does not fail the send
        step_t s = run(m, "AT+QICLOSE=0", NULL, BG95_CMD_TIMEOUT_MS);
        if (s != STEP_WAIT) {
            m->send_status = BG95_SEND_DONE;
            enter(m, BG95_READY);
        }
        break;
    }
    }
}

static void st_recovery(bg95_t *m)
{
    if (m->recoveries <= 1U) {
        // Level 1: radio off/on, keep the module powered.
        step_t s = run(m, (m->step == 0U) ? "AT+CFUN=0" : "AT+CFUN=1", NULL, 15000U);
        if (s == STEP_DONE) {
            if (++m->step == 2U) {
                m->reg_stat = 0;
                enter(m, BG95_REGISTER);
            }
        } else if (s == STEP_FAILED) {
            m->recoveries++;  // radio commands fail: escalate to a power cycle
            enter(m, BG95_RECOVERY);
        } else {
            // in flight
        }
        return;
    }
    // Level 2+: power cycle. Try a clean AT+QPOWD; if the module does not answer, pulse PWRKEY anyway.
    if (m->step == 0U) {
        step_t s = run(m, "AT+QPOWD=1", NULL, BG95_CMD_TIMEOUT_MS);
        if (s != STEP_WAIT) {
            m->step = 1U;
            m->state_since_ms = m->now_ms;
        }
    } else if (in_state_ms(m) >= 2000U) {  // let it shut down
        enter(m, BG95_POWER_ON);
    } else {
        // waiting
    }
}

// ---------------------------------------------------------------- API

void bg95_init(bg95_t *m, at_io_t io, bg95_hw_t hw, const bg95_config_t *cfg)
{
    memset(m, 0, sizeof *m);
    m->hw = hw;
    m->cfg = *cfg;
    at_engine_init(&m->at, io, urcs, sizeof urcs / sizeof urcs[0], m);
    m->state = BG95_OFF;
}

void bg95_start(bg95_t *m, uint32_t now_ms)
{
    m->now_ms = now_ms;
    m->recoveries = 0U;
    enter(m, BG95_POWER_ON);
}

void bg95_feed(bg95_t *m, const uint8_t *data, size_t len)
{
    at_engine_feed(&m->at, data, len);
}

bool bg95_send(bg95_t *m, const uint8_t *data, size_t len)
{
    if ((m->state != BG95_READY) || (len == 0U) || (len > BG95_PAYLOAD_MAX)) {
        return false;
    }
    memcpy(m->payload, data, len);
    m->payload_len = len;
    m->send_status = BG95_SEND_BUSY;
    enter(m, BG95_SENDING);
    return true;
}

void bg95_poll(bg95_t *m, uint32_t now_ms)
{
    m->now_ms = now_ms;
    at_engine_poll(&m->at, now_ms);

    if (m->rebooted) {
        // Settings are lost after a restart: run the init script again. A send in progress is lost.
        m->rebooted = false;
        if (m->state == BG95_SENDING) {
            m->send_status = BG95_SEND_FAILED;
        }
        m->cmd_active = false;
        enter(m, BG95_INIT);
        return;
    }

    switch (m->state) {
    case BG95_POWER_ON:
        st_power_on(m);
        break;
    case BG95_WAIT_RDY:
        st_wait_rdy(m);
        break;
    case BG95_INIT:
        st_init(m);
        break;
    case BG95_REGISTER:
        st_register(m);
        break;
    case BG95_ATTACH:
        st_attach(m);
        break;
    case BG95_SENDING:
        st_sending(m);
        break;
    case BG95_RECOVERY:
        st_recovery(m);
        break;
    case BG95_OFF:
    case BG95_READY:
    case BG95_FAILED:
    default:
        break;
    }
}

const char *bg95_state_name(bg95_state_t s)
{
    static const char *const names[] = {"OFF",    "POWER_ON", "WAIT_RDY", "INIT",     "REGISTER",
                                        "ATTACH", "READY",    "SENDING",  "RECOVERY", "FAILED"};
    return ((unsigned)s < (sizeof names / sizeof names[0])) ? names[s] : "?";
}
