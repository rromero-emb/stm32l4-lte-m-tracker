#ifndef RINGBUF_H
#define RINGBUF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Single-producer / single-consumer byte ring buffer (e.g. producer = UART IRQ, consumer = main loop).
// Size must be a power of two. One slot is kept free to tell "full" from "empty".
typedef struct {
    uint8_t *buf;
    size_t size;
    volatile size_t head;  // written only by the producer
    volatile size_t tail;  // written only by the consumer
} ringbuf_t;

bool ringbuf_init(ringbuf_t *rb, uint8_t *storage, size_t size);
size_t ringbuf_used(const ringbuf_t *rb);
size_t ringbuf_free(const ringbuf_t *rb);
// Returns the number of bytes actually written / read.
size_t ringbuf_write(ringbuf_t *rb, const uint8_t *data, size_t len);
size_t ringbuf_read(ringbuf_t *rb, uint8_t *data, size_t len);

// Consume what a circular-mode DMA has written since the last call and copy it into `out`.
// `pos` is the DMA write position (buffer size - CNDTR), `*last` the position consumed so far.
// Handles the wrap-around. Returns the number of bytes copied (bytes that did not fit in `out`
// are dropped and counted in the return value of ringbuf_write, i.e. not copied).
size_t dma_circ_consume(const uint8_t *dma_buf, size_t dma_size, size_t *last, size_t pos, ringbuf_t *out);

#endif
