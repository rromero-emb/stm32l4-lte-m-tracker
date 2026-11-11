#include "ringbuf.h"

bool ringbuf_init(ringbuf_t *rb, uint8_t *storage, size_t size)
{
    if ((rb == NULL) || (storage == NULL) || (size < 2U) || ((size & (size - 1U)) != 0U)) {
        return false;
    }
    rb->buf = storage;
    rb->size = size;
    rb->head = 0U;
    rb->tail = 0U;
    return true;
}

size_t ringbuf_used(const ringbuf_t *rb)
{
    return (rb->head - rb->tail) & (rb->size - 1U);
}

size_t ringbuf_free(const ringbuf_t *rb)
{
    return rb->size - 1U - ringbuf_used(rb);
}

size_t ringbuf_write(ringbuf_t *rb, const uint8_t *data, size_t len)
{
    size_t n = 0U;
    size_t head = rb->head;
    size_t room = ringbuf_free(rb);
    while ((n < len) && (n < room)) {
        rb->buf[head] = data[n];
        head = (head + 1U) & (rb->size - 1U);
        n++;
    }
    rb->head = head;  // publish after the data is in place
    return n;
}

size_t ringbuf_read(ringbuf_t *rb, uint8_t *data, size_t len)
{
    size_t n = 0U;
    size_t tail = rb->tail;
    size_t avail = ringbuf_used(rb);
    while ((n < len) && (n < avail)) {
        data[n] = rb->buf[tail];
        tail = (tail + 1U) & (rb->size - 1U);
        n++;
    }
    rb->tail = tail;
    return n;
}

size_t dma_circ_consume(const uint8_t *dma_buf, size_t dma_size, size_t *last, size_t pos, ringbuf_t *out)
{
    size_t copied = 0U;
    if (pos >= dma_size) {
        pos = 0U;  // CNDTR reload instant: position dma_size is the same as 0
    }
    if (pos > *last) {
        copied += ringbuf_write(out, &dma_buf[*last], pos - *last);
    } else if (pos < *last) {
        copied += ringbuf_write(out, &dma_buf[*last], dma_size - *last);
        copied += ringbuf_write(out, dma_buf, pos);
    } else {
        // pos == last: nothing new (a full lap without servicing cannot be detected here;
        // the half/complete-transfer interrupts make sure we are called at least twice per lap).
    }
    *last = pos;
    return copied;
}
