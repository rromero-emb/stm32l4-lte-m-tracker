#ifndef BG95_H
#define BG95_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "at.h"

// Quectel BG95 (LTE-M / NB-IoT) state machine on top of the AT engine. Non-blocking: call
// bg95_poll() periodically with the current time; feed received bytes with bg95_feed().
//
//   OFF -> POWER_ON (PWRKEY pulse) -> WAIT_RDY ("APP RDY") -> INIT (script) -> REGISTER (+CEREG)
//       -> ATTACH (PDP context) -> READY <-> SENDING (UDP: QIOPEN / QISEND '>' payload / QICLOSE)
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
#define BG95_PAYLOAD_MAX 64U

typedef enum {
    BG95_OFF = 0,
    BG95_POWER_ON,
    BG95_WAIT_RDY,
    BG95_INIT,
    BG95_REGISTER,
    BG95_ATTACH,
    BG95_READY,
    BG95_SENDING,
    BG95_RECOVERY,
    BG95_FAILED,
} bg95_state_t;

typedef enum { BG95_SEND_NONE = 0, BG95_SEND_BUSY, BG95_SEND_DONE, BG95_SEND_FAILED } bg95_send_status_t;

typedef struct {
    void (*pwrkey)(void *hw, bool asserted);  // drive the PWRKEY line (asserted = pulled low)
    void *hw;
} bg95_hw_t;

typedef struct {
    const char *apn;
    const char *host;
    uint16_t port;
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

static inline bg95_state_t bg95_state(const bg95_t *m)
{
    return m->state;
}

static inline bg95_send_status_t bg95_send_status(const bg95_t *m)
{
    return m->send_status;
}

const char *bg95_state_name(bg95_state_t s);

#endif
