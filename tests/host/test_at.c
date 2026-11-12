#include <string.h>

#include "at.h"
#include "unity.h"

// ---------------------------------------------------------------- parser

static char lines[8][AT_LINE_MAX];
static size_t n_lines;

static void collect(void *ctx, const char *line)
{
    (void)ctx;
    if (n_lines < 8U) {
        strcpy(lines[n_lines++], line);
    }
}

static void feed_str(at_parser_t *p, const char *s)
{
    at_parser_feed(p, (const uint8_t *)s, strlen(s));
}

static at_parser_t parser;

// ---------------------------------------------------------------- engine

static char written[256];
static size_t written_len;

static void capture_write(void *io, const char *data, size_t len)
{
    (void)io;
    memcpy(&written[written_len], data, len);
    written_len += len;
    written[written_len] = '\0';
}

static char urc_seen[AT_LINE_MAX];
static unsigned urc_count;

static void on_urc(void *ctx, const char *line)
{
    (void)ctx;
    strcpy(urc_seen, line);
    urc_count++;
}

static const at_urc_t urcs[] = {{"+CEREG:", on_urc}, {"+QIURC:", on_urc}};
static at_engine_t eng;

static void efeed(const char *s)
{
    at_engine_feed(&eng, (const uint8_t *)s, strlen(s));
}

void setUp(void)
{
    n_lines = 0U;
    memset(lines, 0, sizeof lines);
    at_parser_init(&parser, collect, NULL);
    written_len = 0U;
    written[0] = '\0';
    urc_count = 0U;
    urc_seen[0] = '\0';
    at_io_t io = {capture_write, NULL};
    at_engine_init(&eng, io, urcs, 2U, NULL);
}

void tearDown(void) {}

static void test_lines_split_across_reads(void)
{
    feed_str(&parser, "\r\n+CEREG: 1,");
    TEST_ASSERT_EQUAL_size_t(0U, n_lines);
    feed_str(&parser, "5\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_size_t(2U, n_lines);
    TEST_ASSERT_EQUAL_STRING("+CEREG: 1,5", lines[0]);
    TEST_ASSERT_EQUAL_STRING("OK", lines[1]);
}

static void test_prompt_without_line_end(void)
{
    feed_str(&parser, "\r\n> ");
    TEST_ASSERT_EQUAL_size_t(1U, n_lines);
    TEST_ASSERT_EQUAL_STRING(">", lines[0]);
    feed_str(&parser, "\r\nSEND OK\r\n");
    TEST_ASSERT_EQUAL_STRING("SEND OK", lines[1]);  // the space after '>' was swallowed
}

static void test_overlong_line_dropped_whole(void)
{
    char big[AT_LINE_MAX + 20];
    memset(big, 'x', sizeof big - 1U);
    big[sizeof big - 1U] = '\0';
    feed_str(&parser, big);
    feed_str(&parser, "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_size_t(1U, n_lines);
    TEST_ASSERT_EQUAL_STRING("OK", lines[0]);
    TEST_ASSERT_EQUAL_UINT32(1U, parser.dropped_lines);
}

static void test_send_appends_cr_and_rejects_second_command(void)
{
    TEST_ASSERT_TRUE(at_send(&eng, "AT+CSQ", "+CSQ:", false, 1000U, 0U));
    TEST_ASSERT_EQUAL_STRING("AT+CSQ\r", written);
    TEST_ASSERT_FALSE(at_send(&eng, "AT", NULL, false, 1000U, 0U));
    TEST_ASSERT_EQUAL(AT_PENDING, at_result(&eng));
}

static void test_response_captured_and_ok(void)
{
    at_send(&eng, "AT+CSQ", "+CSQ:", false, 1000U, 0U);
    efeed("\r\n+CSQ: 21,99\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL(AT_OK, at_result(&eng));
    TEST_ASSERT_EQUAL_STRING("+CSQ: 21,99", at_response(&eng));
    int32_t v[2];
    TEST_ASSERT_EQUAL_size_t(2U, at_parse_ints(at_response(&eng), v, 2U));
    TEST_ASSERT_EQUAL_INT32(21, v[0]);
    TEST_ASSERT_EQUAL_INT32(99, v[1]);
}

static void test_same_prefix_is_response_when_expected_urc_otherwise(void)
{
    at_send(&eng, "AT+CEREG?", "+CEREG:", false, 1000U, 0U);
    efeed("\r\n+CEREG: 1,2\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_UINT(0U, urc_count);
    TEST_ASSERT_EQUAL_STRING("+CEREG: 1,2", at_response(&eng));

    efeed("\r\n+CEREG: 5\r\n");  // no command in flight: unsolicited
    TEST_ASSERT_EQUAL_UINT(1U, urc_count);
    TEST_ASSERT_EQUAL_STRING("+CEREG: 5", urc_seen);
}

static void test_urc_in_the_middle_of_a_response(void)
{
    at_send(&eng, "AT+CSQ", "+CSQ:", false, 1000U, 0U);
    efeed("\r\n+QIURC: \"recv\",0\r\n\r\n+CSQ: 10,99\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_UINT(1U, urc_count);
    TEST_ASSERT_EQUAL(AT_OK, at_result(&eng));
    TEST_ASSERT_EQUAL_STRING("+CSQ: 10,99", at_response(&eng));
}

static void test_cme_error_code(void)
{
    at_send(&eng, "AT+QGPSLOC=2", "+QGPSLOC:", false, 1000U, 0U);
    efeed("\r\n+CME ERROR: 516\r\n");  // 516 = GNSS: not fixed yet
    TEST_ASSERT_EQUAL(AT_CME_ERROR, at_result(&eng));
    TEST_ASSERT_EQUAL_INT(516, eng.cme);
    TEST_ASSERT_NULL(at_response(&eng));
}

static void test_plain_error(void)
{
    at_send(&eng, "AT+BOGUS", NULL, false, 1000U, 0U);
    efeed("\r\nERROR\r\n");
    TEST_ASSERT_EQUAL(AT_ERROR, at_result(&eng));
}

static void test_echo_ignored(void)
{
    at_send(&eng, "AT", NULL, false, 1000U, 0U);
    efeed("AT\r\r\nOK\r\n");
    TEST_ASSERT_EQUAL(AT_OK, at_result(&eng));
    TEST_ASSERT_EQUAL_UINT32(0U, eng.unknown_lines);
}

static void test_timeout_across_millisecond_wraparound(void)
{
    uint32_t start = 0xFFFFFF00U;
    at_send(&eng, "AT", NULL, false, 0x200U, start);
    at_engine_poll(&eng, start + 0x100U);  // wrapped to 0x00000000, not yet expired
    TEST_ASSERT_EQUAL(AT_PENDING, at_result(&eng));
    at_engine_poll(&eng, start + 0x200U);
    TEST_ASSERT_EQUAL(AT_TIMEOUT, at_result(&eng));
    TEST_ASSERT_TRUE(at_send(&eng, "AT", NULL, false, 100U, start + 0x200U));  // engine free again
}

static void test_prompt_then_payload_then_send_ok(void)
{
    at_send(&eng, "AT+QISEND=0,3", NULL, true, 1000U, 0U);
    efeed("\r\n> ");
    TEST_ASSERT_EQUAL(AT_PROMPT, at_result(&eng));
    written_len = 0U;
    at_write_raw(&eng, (const uint8_t *)"abc", 3U, 1000U, 0U);
    TEST_ASSERT_EQUAL_STRING("abc", written);  // no "\r" appended to raw data
    TEST_ASSERT_EQUAL(AT_PENDING, at_result(&eng));
    efeed("\r\nSEND OK\r\n");
    TEST_ASSERT_EQUAL(AT_OK, at_result(&eng));
}

static void test_parse_ints_skips_quoted_fields(void)
{
    int32_t v[4] = {0, 0, 0, 0};
    TEST_ASSERT_EQUAL_size_t(4U, at_parse_ints("+CEREG: 2,5,\"1A2B\",-7", v, 4U));
    TEST_ASSERT_EQUAL_INT32(2, v[0]);
    TEST_ASSERT_EQUAL_INT32(5, v[1]);
    TEST_ASSERT_EQUAL_INT32(0, v[2]);  // quoted: untouched
    TEST_ASSERT_EQUAL_INT32(-7, v[3]);
    TEST_ASSERT_EQUAL_size_t(0U, at_parse_ints("OK", v, 4U));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_lines_split_across_reads);
    RUN_TEST(test_prompt_without_line_end);
    RUN_TEST(test_overlong_line_dropped_whole);
    RUN_TEST(test_send_appends_cr_and_rejects_second_command);
    RUN_TEST(test_response_captured_and_ok);
    RUN_TEST(test_same_prefix_is_response_when_expected_urc_otherwise);
    RUN_TEST(test_urc_in_the_middle_of_a_response);
    RUN_TEST(test_cme_error_code);
    RUN_TEST(test_plain_error);
    RUN_TEST(test_echo_ignored);
    RUN_TEST(test_timeout_across_millisecond_wraparound);
    RUN_TEST(test_prompt_then_payload_then_send_ok);
    RUN_TEST(test_parse_ints_skips_quoted_fields);
    return UNITY_END();
}
