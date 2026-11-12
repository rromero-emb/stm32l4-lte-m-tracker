#include "at.h"

#include <string.h>

// ---------------------------------------------------------------- parser

void at_parser_init(at_parser_t *p, at_line_cb_t on_line, void *ctx)
{
    memset(p, 0, sizeof *p);
    p->on_line = on_line;
    p->ctx = ctx;
}

static void emit(at_parser_t *p)
{
    if (p->overflow) {
        p->dropped_lines++;
    } else if (p->len > 0U) {
        p->buf[p->len] = '\0';
        p->on_line(p->ctx, p->buf);
    } else {
        // empty line between "\r\n" pairs: nothing to report
    }
    p->len = 0U;
    p->overflow = false;
}

void at_parser_feed(at_parser_t *p, const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        char c = (char)data[i];
        if ((c == '\r') || (c == '\n')) {
            emit(p);
        } else if ((c == '>') && (p->len == 0U)) {
            // Data prompt: "\r\n> " with no line terminator after it.
            p->buf[0] = '>';
            p->len = 1U;
            emit(p);
            p->skip_space = true;
        } else if ((c == ' ') && p->skip_space && (p->len == 0U)) {
            p->skip_space = false;  // the space that follows the prompt
        } else if (p->len < (AT_LINE_MAX - 1U)) {
            p->skip_space = false;
            p->buf[p->len++] = c;
        } else {
            p->overflow = true;
        }
    }
}

// ---------------------------------------------------------------- engine

static bool starts_with(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

static void finish(at_engine_t *e, at_result_t r)
{
    e->result = r;
    e->busy = (r == AT_PROMPT);  // after a prompt the same command continues
}

static void on_line(void *ctx, const char *line)
{
    at_engine_t *e = (at_engine_t *)ctx;

    if (e->busy) {
        if (strcmp(line, "OK") == 0) {
            finish(e, AT_OK);
            return;
        }
        if ((strcmp(line, "ERROR") == 0) || (strcmp(line, "SEND FAIL") == 0)) {
            finish(e, AT_ERROR);
            return;
        }
        if (starts_with(line, "+CME ERROR:")) {
            e->cme = 0;
            int32_t code = 0;
            if (at_parse_ints(line, &code, 1U) == 1U) {
                e->cme = (int)code;
            }
            finish(e, AT_CME_ERROR);
            return;
        }
        if ((strcmp(line, ">") == 0) && e->expect_prompt) {
            finish(e, AT_PROMPT);
            return;
        }
        if (strcmp(line, "SEND OK") == 0) {
            finish(e, AT_OK);
            return;
        }
        if ((e->expect != NULL) && starts_with(line, e->expect)) {
            size_t n = strlen(line);
            if (n >= AT_RESP_MAX) {
                n = AT_RESP_MAX - 1U;
            }
            memcpy(e->resp, line, n);
            e->resp[n] = '\0';
            e->have_resp = true;
            return;
        }
        if (starts_with(line, "AT")) {
            return;  // command echo (ATE1): ignore
        }
    }

    for (size_t i = 0; i < e->n_urcs; i++) {
        if (starts_with(line, e->urcs[i].prefix)) {
            e->urcs[i].handler(e->urc_ctx, line);
            return;
        }
    }
    e->unknown_lines++;
}

void at_engine_init(at_engine_t *e, at_io_t io, const at_urc_t *urcs, size_t n_urcs, void *urc_ctx)
{
    memset(e, 0, sizeof *e);
    e->io = io;
    e->urcs = urcs;
    e->n_urcs = n_urcs;
    e->urc_ctx = urc_ctx;
    e->result = AT_OK;  // idle
    at_parser_init(&e->parser, on_line, e);
}

bool at_send(at_engine_t *e, const char *cmd, const char *expect, bool expect_prompt, uint32_t timeout_ms,
             uint32_t now_ms)
{
    if (e->busy) {
        return false;
    }
    e->busy = true;
    e->expect = expect;
    e->expect_prompt = expect_prompt;
    e->deadline_ms = now_ms + timeout_ms;
    e->result = AT_PENDING;
    e->cme = 0;
    e->have_resp = false;
    e->resp[0] = '\0';
    e->io.write(e->io.io, cmd, strlen(cmd));
    e->io.write(e->io.io, "\r", 1U);
    return true;
}

void at_write_raw(at_engine_t *e, const uint8_t *data, size_t len, uint32_t timeout_ms, uint32_t now_ms)
{
    e->busy = true;
    e->expect_prompt = false;
    e->result = AT_PENDING;
    e->deadline_ms = now_ms + timeout_ms;
    e->io.write(e->io.io, (const char *)data, len);
}

void at_engine_feed(at_engine_t *e, const uint8_t *data, size_t len)
{
    at_parser_feed(&e->parser, data, len);
}

void at_engine_poll(at_engine_t *e, uint32_t now_ms)
{
    // Signed difference: correct across the 32-bit millisecond wrap-around.
    if (e->busy && (e->result == AT_PENDING) && ((int32_t)(now_ms - e->deadline_ms) >= 0)) {
        finish(e, AT_TIMEOUT);
    }
}

// ---------------------------------------------------------------- helpers

size_t at_parse_ints(const char *line, int32_t *out, size_t max)
{
    const char *p = strchr(line, ':');
    if (p == NULL) {
        return 0U;
    }
    p++;
    size_t n = 0U;
    while ((*p != '\0') && (n < max)) {
        while (*p == ' ') {
            p++;
        }
        if (*p == '"') {  // quoted field: skip it
            p = strchr(p + 1, '"');
            if (p == NULL) {
                return n;
            }
            p++;
        } else {
            bool neg = (*p == '-');
            if (neg) {
                p++;
            }
            if ((*p >= '0') && (*p <= '9')) {
                int32_t v = 0;
                while ((*p >= '0') && (*p <= '9')) {
                    v = (v * 10) + (int32_t)(*p - '0');
                    p++;
                }
                out[n] = neg ? -v : v;
            }
        }
        n++;
        while ((*p != ',') && (*p != '\0')) {
            p++;
        }
        if (*p == ',') {
            p++;
        }
    }
    return n;
}
