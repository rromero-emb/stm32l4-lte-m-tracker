#include <limits.h>
#include <stdarg.h>
#include <string.h>

#include "fmt.h"
#include "unity.h"

static char buf[64];

// Same as fmt_snprintf but without the printf format attribute, so deliberately malformed
// formats can be tested without tripping -Wformat.
static size_t raw(char *b, size_t n, const char *f, ...)
{
    va_list ap;
    va_start(ap, f);
    size_t r = fmt_vsnprintf(b, n, f, ap);
    va_end(ap);
    return r;
}

void setUp(void)
{
    memset(buf, 'Z', sizeof buf);
}

void tearDown(void) {}

static void test_plain_text_and_percent(void)
{
    TEST_ASSERT_EQUAL_size_t(7U, fmt_snprintf(buf, sizeof buf, "100%% ok"));
    TEST_ASSERT_EQUAL_STRING("100% ok", buf);
}

static void test_integers(void)
{
    fmt_snprintf(buf, sizeof buf, "%d %i %u", -42, 7, 4000000000U);
    TEST_ASSERT_EQUAL_STRING("-42 7 4000000000", buf);
    fmt_snprintf(buf, sizeof buf, "%d", INT_MIN);
    TEST_ASSERT_EQUAL_STRING("-2147483648", buf);
}

static void test_hex_and_padding(void)
{
    raw(buf, sizeof buf, "0x%08x 0x%X %4u|%-", 0x2001FFF8U, 0xBEEFU, 42U);
    TEST_ASSERT_EQUAL_STRING("0x2001fff8 0xBEEF   42|%-", buf);
    fmt_snprintf(buf, sizeof buf, "%05d", -12);
    TEST_ASSERT_EQUAL_STRING("-0012", buf);
}

static void test_strings_and_chars(void)
{
    raw(buf, sizeof buf, "[%s|%c|%s]", "spi", 'x', (const char *)NULL);
    TEST_ASSERT_EQUAL_STRING("[spi|x|(null)]", buf);
}

static void test_truncates_and_terminates(void)
{
    char small[5];
    TEST_ASSERT_EQUAL_size_t(4U, fmt_snprintf(small, sizeof small, "abcdefgh"));
    TEST_ASSERT_EQUAL_STRING("abcd", small);
}

static void test_trailing_percent(void)
{
    raw(buf, sizeof buf, "end%");
    TEST_ASSERT_EQUAL_STRING("end", buf);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_plain_text_and_percent);
    RUN_TEST(test_integers);
    RUN_TEST(test_hex_and_padding);
    RUN_TEST(test_strings_and_chars);
    RUN_TEST(test_truncates_and_terminates);
    RUN_TEST(test_trailing_percent);
    return UNITY_END();
}
