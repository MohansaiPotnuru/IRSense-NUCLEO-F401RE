/**
 * @file    ring_buffer.c
 * @brief   Lock-free single-producer / single-consumer byte FIFO.
 */
#include "app/ring_buffer.h"
#include "app/util.h"

#include <stddef.h>

bool rb_init(ring_buffer_t *rb, uint8_t *storage, uint32_t capacity)
{
    if (rb == NULL || storage == NULL || !is_pow2_u32(capacity)) {
        return false;
    }
    rb->buf  = storage;
    rb->mask = capacity - 1u;
    rb->head = 0u;
    rb->tail = 0u;
    return true;
}

void rb_clear(ring_buffer_t *rb)
{
    rb->tail = rb->head;
}

uint32_t rb_capacity(const ring_buffer_t *rb)
{
    return rb->mask + 1u;
}

uint32_t rb_count(const ring_buffer_t *rb)
{
    return rb->head - rb->tail;
}

uint32_t rb_free(const ring_buffer_t *rb)
{
    return rb_capacity(rb) - rb_count(rb);
}

bool rb_is_empty(const ring_buffer_t *rb)
{
    return rb->head == rb->tail;
}

bool rb_is_full(const ring_buffer_t *rb)
{
    return rb_count(rb) > rb->mask;
}

bool rb_put(ring_buffer_t *rb, uint8_t byte)
{
    uint32_t head = rb->head;
    if ((head - rb->tail) > rb->mask) {
        return false;
    }
    rb->buf[head & rb->mask] = byte;
    COMPILER_BARRIER(); /* data must land before the new head is visible */
    rb->head = head + 1u;
    return true;
}

bool rb_get(ring_buffer_t *rb, uint8_t *byte)
{
    uint32_t tail = rb->tail;
    if (rb->head == tail) {
        return false;
    }
    *byte = rb->buf[tail & rb->mask];
    COMPILER_BARRIER(); /* read the slot before releasing it */
    rb->tail = tail + 1u;
    return true;
}

bool rb_peek(const ring_buffer_t *rb, uint8_t *byte)
{
    uint32_t tail = rb->tail;
    if (rb->head == tail) {
        return false;
    }
    *byte = rb->buf[tail & rb->mask];
    return true;
}

uint32_t rb_write(ring_buffer_t *rb, const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t n = 0u;
    while (n < len && rb_put(rb, p[n])) {
        n++;
    }
    return n;
}

uint32_t rb_read(ring_buffer_t *rb, void *data, uint32_t len)
{
    uint8_t *p = (uint8_t *)data;
    uint32_t n = 0u;
    while (n < len && rb_get(rb, &p[n])) {
        n++;
    }
    return n;
}
