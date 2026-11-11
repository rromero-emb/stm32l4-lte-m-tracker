#include <string.h>

#include "ringbuf.h"
#include "unity.h"

static uint8_t storage[8];
static ringbuf_t rb;

void setUp(void)
{
    memset(storage, 0, sizeof storage);
    TEST_ASSERT_TRUE(ringbuf_init(&rb, storage, sizeof storage));
}

void tearDown(void) {}

static void test_size_must_be_power_of_two(void)
{
    ringbuf_t r;
    uint8_t s[6];
    TEST_ASSERT_FALSE(ringbuf_init(&r, s, sizeof s));
    TEST_ASSERT_FALSE(ringbuf_init(&r, s, 1U));
    TEST_ASSERT_FALSE(ringbuf_init(&r, NULL, 8U));
}

static void test_write_then_read(void)
{
    const uint8_t in[3] = {1, 2, 3};
    uint8_t out[3] = {0};
    TEST_ASSERT_EQUAL_size_t(3U, ringbuf_write(&rb, in, 3U));
    TEST_ASSERT_EQUAL_size_t(3U, ringbuf_used(&rb));
    TEST_ASSERT_EQUAL_size_t(3U, ringbuf_read(&rb, out, 3U));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(in, out, 3U);
    TEST_ASSERT_EQUAL_size_t(0U, ringbuf_used(&rb));
}

static void test_capacity_is_size_minus_one(void)
{
    const uint8_t in[10] = {0};
    TEST_ASSERT_EQUAL_size_t(7U, ringbuf_write(&rb, in, sizeof in));
    TEST_ASSERT_EQUAL_size_t(0U, ringbuf_free(&rb));
    TEST_ASSERT_EQUAL_size_t(0U, ringbuf_write(&rb, in, 1U));
}

static void test_wraps_around(void)
{
    uint8_t out[8];
    const uint8_t a[5] = {1, 2, 3, 4, 5};
    const uint8_t b[5] = {6, 7, 8, 9, 10};
    TEST_ASSERT_EQUAL_size_t(5U, ringbuf_write(&rb, a, 5U));
    TEST_ASSERT_EQUAL_size_t(5U, ringbuf_read(&rb, out, 5U));
    TEST_ASSERT_EQUAL_size_t(5U, ringbuf_write(&rb, b, 5U));  // crosses the end of storage
    TEST_ASSERT_EQUAL_size_t(5U, ringbuf_read(&rb, out, 8U));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(b, out, 5U);
}

static void test_dma_linear_chunk(void)
{
    const uint8_t dma[8] = {'A', 'T', '\r', 0, 0, 0, 0, 0};
    size_t last = 0U;
    uint8_t out[8];
    TEST_ASSERT_EQUAL_size_t(3U, dma_circ_consume(dma, 8U, &last, 3U, &rb));
    TEST_ASSERT_EQUAL_size_t(3U, last);
    TEST_ASSERT_EQUAL_size_t(3U, ringbuf_read(&rb, out, sizeof out));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(dma, out, 3U);
}

static void test_dma_wrapped_chunk(void)
{
    // DMA wrote "XYZ" at 6,7 then 0: data from last=6 to pos=1 crosses the end of the buffer.
    const uint8_t dma[8] = {'Z', 0, 0, 0, 0, 0, 'X', 'Y'};
    size_t last = 6U;
    uint8_t out[8];
    TEST_ASSERT_EQUAL_size_t(3U, dma_circ_consume(dma, 8U, &last, 1U, &rb));
    TEST_ASSERT_EQUAL_size_t(1U, last);
    TEST_ASSERT_EQUAL_size_t(3U, ringbuf_read(&rb, out, sizeof out));
    TEST_ASSERT_EQUAL_MEMORY("XYZ", out, 3U);
}

static void test_dma_position_at_end_means_zero(void)
{
    const uint8_t dma[8] = {0, 0, 0, 0, 0, 0, 'E', 'N'};
    size_t last = 6U;
    TEST_ASSERT_EQUAL_size_t(2U, dma_circ_consume(dma, 8U, &last, 8U, &rb));
    TEST_ASSERT_EQUAL_size_t(0U, last);
}

static void test_dma_nothing_new(void)
{
    const uint8_t dma[8] = {0};
    size_t last = 4U;
    TEST_ASSERT_EQUAL_size_t(0U, dma_circ_consume(dma, 8U, &last, 4U, &rb));
}

static void test_dma_overflow_drops_excess(void)
{
    uint8_t big_storage[4];
    ringbuf_t small;
    TEST_ASSERT_TRUE(ringbuf_init(&small, big_storage, sizeof big_storage));  // holds 3
    const uint8_t dma[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    size_t last = 0U;
    TEST_ASSERT_EQUAL_size_t(3U, dma_circ_consume(dma, 8U, &last, 6U, &small));
    TEST_ASSERT_EQUAL_size_t(6U, last);  // position still advances: no stale data on the next call
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_size_must_be_power_of_two);
    RUN_TEST(test_write_then_read);
    RUN_TEST(test_capacity_is_size_minus_one);
    RUN_TEST(test_wraps_around);
    RUN_TEST(test_dma_linear_chunk);
    RUN_TEST(test_dma_wrapped_chunk);
    RUN_TEST(test_dma_position_at_end_means_zero);
    RUN_TEST(test_dma_nothing_new);
    RUN_TEST(test_dma_overflow_drops_excess);
    return UNITY_END();
}
