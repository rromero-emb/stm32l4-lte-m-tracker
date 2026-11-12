// Scripted stand-in for a Quectel BG95 on the other end of the UART.
// Each step: the exact command line the firmware must send, and the bytes the "modem" answers.
// A step whose reply contains "> " switches to payload mode: the next `n` raw bytes (n taken from
// AT+QISEND=0,<n>) are matched against the following step's `cmd`.
#ifndef FAKE_MODEM_H
#define FAKE_MODEM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *cmd;    // expected command (without "\r"), or the raw payload
    const char *reply;  // bytes sent back, e.g. "\r\nOK\r\n"
} fake_step_t;

typedef struct {
    const fake_step_t *script;
    size_t n_steps;
    size_t pos;

    char line[256];
    size_t len;
    bool raw_mode;
    size_t raw_left;

    char out[1024];  // bytes waiting to be delivered to the firmware
    size_t out_len;

    unsigned unexpected;  // commands that did not match the script
    char last_cmd[128];

    bool pwrkey_low;
    uint32_t pwrkey_low_since;
    uint32_t pwrkey_last_pulse_ms;
    unsigned pwrkey_pulses;
} fake_modem_t;

void fake_init(fake_modem_t *f, const fake_step_t *script, size_t n_steps);
// at_io_t.write target
void fake_write(void *io, const char *data, size_t len);
// bg95_hw_t.pwrkey target; `now` is read from fake_now
void fake_pwrkey(void *hw, bool asserted);
extern uint32_t fake_now;
// Spontaneous output from the modem (URCs).
void fake_inject(fake_modem_t *f, const char *text);
size_t fake_take(fake_modem_t *f, uint8_t *buf, size_t max);
bool fake_script_done(const fake_modem_t *f);

#endif
