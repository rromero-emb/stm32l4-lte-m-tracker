#ifndef AT_H
#define AT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// AT command layer for a cellular modem, transport- and RTOS-independent.
//
//  bytes -> at_parser (line framing, '>' prompt) -> at_engine (one command in flight)
//                                                      |-> final result: OK / ERROR / +CME ERROR / timeout
//                                                      |-> information line matching the expected prefix
//                                                      '-> URC table (unsolicited result codes)
//
// A line that starts with the expected prefix of the command in flight is its response; otherwise,
// if it matches a URC prefix, it is a URC. This resolves prefixes that are both (e.g. "+CEREG:").

#define AT_LINE_MAX 128U
#define AT_RESP_MAX 128U

typedef void (*at_line_cb_t)(void *ctx, const char *line);

typedef struct {
    char buf[AT_LINE_MAX];
    size_t len;
    bool overflow;    // current line exceeded AT_LINE_MAX: it is dropped whole
    bool skip_space;  // swallow the ' ' that follows the '>' prompt
    at_line_cb_t on_line;
    void *ctx;
    uint32_t dropped_lines;
} at_parser_t;

void at_parser_init(at_parser_t *p, at_line_cb_t on_line, void *ctx);
// Emits every non-empty line without CR/LF. A lone '>' (data prompt, not followed by CR/LF) is
// emitted as the line ">" as soon as it arrives.
void at_parser_feed(at_parser_t *p, const uint8_t *data, size_t len);

typedef enum {
    AT_PENDING = 0,  // command still in flight
    AT_OK,
    AT_ERROR,      // plain "ERROR"
    AT_CME_ERROR,  // "+CME ERROR: <n>" (needs AT+CMEE=1); code in at_engine_t.cme
    AT_TIMEOUT,
    AT_PROMPT,  // '>' received: write the payload with at_write_raw()
} at_result_t;

typedef struct {
    const char *prefix;  // e.g. "+CEREG:", "RDY", "+QIURC:"
    void (*handler)(void *ctx, const char *line);
} at_urc_t;

typedef struct {
    void (*write)(void *io, const char *data, size_t len);
    void *io;
} at_io_t;

typedef struct {
    at_parser_t parser;
    at_io_t io;
    const at_urc_t *urcs;
    size_t n_urcs;
    void *urc_ctx;

    // Command in flight
    bool busy;
    bool expect_prompt;
    const char *expect;  // information-line prefix of the response, or NULL
    uint32_t deadline_ms;
    at_result_t result;
    int cme;
    char resp[AT_RESP_MAX];  // last line that matched `expect`
    bool have_resp;

    uint32_t unknown_lines;  // lines that were neither response nor known URC
} at_engine_t;

void at_engine_init(at_engine_t *e, at_io_t io, const at_urc_t *urcs, size_t n_urcs, void *urc_ctx);

// Sends "<cmd>\r". Returns false if a command is already in flight.
// expect: prefix of the information response to capture (e.g. "+CEREG:"), or NULL.
// expect_prompt: true for commands that answer with '>' (e.g. AT+QISEND).
bool at_send(at_engine_t *e, const char *cmd, const char *expect, bool expect_prompt, uint32_t timeout_ms,
             uint32_t now_ms);
// Writes raw bytes (payload after a '>' prompt) and keeps waiting for the final result.
void at_write_raw(at_engine_t *e, const uint8_t *data, size_t len, uint32_t timeout_ms, uint32_t now_ms);

void at_engine_feed(at_engine_t *e, const uint8_t *data, size_t len);
// Checks the timeout. Call periodically.
void at_engine_poll(at_engine_t *e, uint32_t now_ms);

// Current result. Once it is not AT_PENDING the engine is free for the next command
// (except AT_PROMPT, which continues with at_write_raw).
static inline at_result_t at_result(const at_engine_t *e)
{
    return e->result;
}

// The captured response line ("+CEREG: 2,5"), or NULL if none.
static inline const char *at_response(const at_engine_t *e)
{
    return e->have_resp ? e->resp : NULL;
}

// Parses up to `max` comma-separated integers after the "<prefix>:" of a response line.
// Quoted fields count as positions but are not parsed (value left untouched). Returns how many
// fields were seen.
size_t at_parse_ints(const char *line, int32_t *out, size_t max);

#endif
