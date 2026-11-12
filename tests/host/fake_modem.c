#include "fake_modem.h"

#include <stdlib.h>
#include <string.h>

uint32_t fake_now;

void fake_init(fake_modem_t *f, const fake_step_t *script, size_t n_steps)
{
    memset(f, 0, sizeof *f);
    f->script = script;
    f->n_steps = n_steps;
}

void fake_inject(fake_modem_t *f, const char *text)
{
    size_t n = strlen(text);
    if (f->out_len + n <= sizeof f->out) {
        memcpy(&f->out[f->out_len], text, n);
        f->out_len += n;
    }
}

static void complete(fake_modem_t *f)
{
    f->line[f->len] = '\0';
    strncpy(f->last_cmd, f->line, sizeof f->last_cmd - 1U);
    f->len = 0U;
    if ((f->pos < f->n_steps) && (strcmp(f->line, f->script[f->pos].cmd) == 0)) {
        const char *reply = f->script[f->pos].reply;
        f->pos++;
        fake_inject(f, reply);
        const char *q = strstr(f->line, "AT+QISEND=0,");
        if ((strstr(reply, "> ") != NULL) && (q != NULL)) {
            f->raw_mode = true;
            f->raw_left = (size_t)strtoul(q + strlen("AT+QISEND=0,"), NULL, 10);
        }
    } else {
        f->unexpected++;  // the real module would answer ERROR or nothing; here: nothing
    }
}

void fake_write(void *io, const char *data, size_t len)
{
    fake_modem_t *f = (fake_modem_t *)io;
    for (size_t i = 0; i < len; i++) {
        if (f->raw_mode) {
            if (f->len < sizeof f->line - 1U) {
                f->line[f->len++] = data[i];
            }
            if (--f->raw_left == 0U) {
                f->raw_mode = false;
                complete(f);
            }
        } else if (data[i] == '\r') {
            complete(f);
        } else if (f->len < sizeof f->line - 1U) {
            f->line[f->len++] = data[i];
        } else {
            // line too long: truncated
        }
    }
}

void fake_pwrkey(void *hw, bool asserted)
{
    fake_modem_t *f = (fake_modem_t *)hw;
    if (asserted && !f->pwrkey_low) {
        f->pwrkey_low = true;
        f->pwrkey_low_since = fake_now;
    } else if (!asserted && f->pwrkey_low) {
        f->pwrkey_low = false;
        f->pwrkey_last_pulse_ms = fake_now - f->pwrkey_low_since;
        f->pwrkey_pulses++;
    } else {
        // no edge
    }
}

size_t fake_take(fake_modem_t *f, uint8_t *buf, size_t max)
{
    size_t n = (f->out_len < max) ? f->out_len : max;
    memcpy(buf, f->out, n);
    memmove(f->out, &f->out[n], f->out_len - n);
    f->out_len -= n;
    return n;
}

bool fake_script_done(const fake_modem_t *f)
{
    return f->pos == f->n_steps;
}
