#ifndef BG95_H
#define BG95_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "at.h"
#include "gnss.h"

// Quectel BG95 (LTE-M / NB-IoT) state machine on top of the AT engine. Non-blocking: call
// bg95_poll() periodically with the current time; feed received bytes with bg95_feed().
//
//   OFF -> POWER_ON (PWRKEY pulse) -> WAIT_RDY ("APP RDY") -> INIT (script) -> REGISTER (+CEREG)
//       -> ATTACH (PDP context) -> READY <-> SENDING (UDP: QIOPEN / QISEND '>' payload / QICLOSE)
//                                         <-> GNSS (QGPS=1, poll QGPSLOC until fix or timeout, QGPSEND)
//   READY or any other state -> POWER_OFF (AT+QPOWD, wait "POWERED DOWN") -> OFF
//   any failure -> RECOVERY: 1st CFUN 0/1, 2nd and later a full power cycle; after
//   BG95_MAX_RECOVERIES the machine stops in FAILED.
//
// Timings from the BG95 hardware design guide: PWRKEY low for 500-1000 ms to switch on;
// the module reports "APP RDY" once the AT interface is usable.

#define BG95_PWRKEY_PULSE_MS 700U
#define BG95_RDY_TIMEOUT_MS 15000U
#define BG95_CMD_TIMEOUT_MS 2000U
#define BG95_REG_POLL_MS 2000U
#define BG95_REG_TIMEOUT_MS 180000U
#define BG95_ATTACH_TIMEOUT_MS 150000U  // AT+QIACT: up to 150 s per the AT manual
#define BG95_OPEN_TIMEOUT_MS 150000U
#define BG95_MAX_RECOVERIES 3U
#define BG95_GNSS_POLL_MS 1000U
#define BG95_POWER_OFF_TIMEOUT_MS 6000U  // "POWERED DOWN" normally follows AT+QPOWD=1 within ~2 s
#define BG95_PAYLOAD_MAX 64U

typedef enum {
    BG95_OFF = 0,
    BG95_POWER_ON,
    BG95_WAIT_RDY,
    BG95_INIT,
    BG95_REGISTER,
    BG95_ATTACH,
    BG95_READY,
    BG95_GNSS,
    BG95_SENDING,
    BG95_RECOVERY,
    BG95_FAILED,
    BG95_POWER_OFF,
} bg95_state_t;

typedef enum { BG95_SEND_NONE = 0, BG95_SEND_BUSY, BG95_SEND_DONE, BG95_SEND_FAILED } bg95_send_status_t;
typedef enum {
    BG95_GNSS_NONE = 0,
    BG95_GNSS_BUSY,
    BG95_GNSS_FIX,
    BG95_GNSS_NO_FIX,
    BG95_GNSS_FAILED
} bg95_gnss_status_t;

typedef struct {
    void (*pwrkey)(void *hw, bool asserted);  // drive the PWRKEY line (asserted = pulled low)
    void *hw;
} bg95_hw_t;

typedef struct {
    const char *apn;
    const char *host;
    uint16_t port;
    // LTE Power Saving Mode requested in the init script (AT+CPSMS) when psm is true.
    bool psm;
    uint32_t psm_tau_s;     // periodic TAU (T3412 extended)
    uint32_t psm_active_s;  // active time after a transfer before PSM (T3324)
} bg95_config_t;

typedef struct {
    at_engine_t at;
    bg95_hw_t hw;
    bg95_config_t cfg;

    bg95_state_t state;
    uint32_t state_since_ms;
    uint32_t now_ms;
    uint8_t step;      // position inside the current state's command sequence
    bool cmd_active;   // a command of this state is in flight
    uint32_t next_ms;  // next poll time (registration)

    bool rdy;       // "APP RDY" seen while powering on
    bool rebooted;  // "APP RDY" seen while up: unexpected module restart
    int reg_stat;   // last +CEREG <stat>: 1 home, 5 roaming, 2 searching, 3 denied
    bool socket_open_urc;
    int socket_open_err;
    bool powered_down;  // "POWERED DOWN" seen
    at_result_t last_result;

    uint32_t gnss_timeout_ms;
    uint32_t gnss_started_ms;
    bg95_gnss_status_t gnss_status;
    gnss_fix_t fix;
    uint16_t ttff_s;

    uint8_t recoveries;
    bg95_state_t failed_in;  // state where the last failure happened

    uint8_t payload[BG95_PAYLOAD_MAX];
    size_t payload_len;
    bg95_send_status_t send_status;
} bg95_t;

void bg95_init(bg95_t *m, at_io_t io, bg95_hw_t hw, const bg95_config_t *cfg);
void bg95_start(bg95_t *m, uint32_t now_ms);
void bg95_feed(bg95_t *m, const uint8_t *data, size_t len);
void bg95_poll(bg95_t *m, uint32_t now_ms);

// Queue one UDP datagram. Only accepted in READY. Progress in bg95_send_status().
bool bg95_send(bg95_t *m, const uint8_t *data, size_t len);

// Start a GNSS fix (GNSS gets RF priority over LTE meanwhile). Only accepted in READY.
// Progress in bg95_gnss_status(); the position in bg95_gnss_fix() once it is BG95_GNSS_FIX.
bool bg95_gnss_start(bg95_t *m, uint32_t timeout_ms);

// Clean shutdown (AT+QPOWD=1), from any state but OFF. A send or fix in progress fails.
bool bg95_power_off(bg95_t *m);

static inline bg95_state_t bg95_state(const bg95_t *m)
{
    return m->state;
}

static inline bg95_send_status_t bg95_send_status(const bg95_t *m)
{
    return m->send_status;
}

static inline bg95_gnss_status_t bg95_gnss_status(const bg95_t *m)
{
    return m->gnss_status;
}

static inline const gnss_fix_t *bg95_gnss_fix(const bg95_t *m)
{
    return &m->fix;
}

// Nothing more will happen without a request: the MCU may stop polling.
static inline bool bg95_is_idle(const bg95_t *m)
{
    return (m->state == BG95_OFF) || (m->state == BG95_FAILED);
}

const char *bg95_state_name(bg95_state_t s);

#endif
