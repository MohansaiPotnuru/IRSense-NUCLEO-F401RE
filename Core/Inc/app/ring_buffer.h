/**
 * @file    ring_buffer.h
 * @brief   Lock-free single-producer / single-consumer byte FIFO.
 *
 * One side may run in an interrupt handler and the other in the main loop
 * without disabling interrupts. Capacity must be a power of two. head and
 * tail are free-running counters, so all `capacity` bytes are usable.
 *
 * Rules: only the producer calls rb_put/rb_write, only the consumer calls
 * rb_get/rb_read/rb_peek/rb_clear.
 */
#ifndef APP_RING_BUFFER_H
#define APP_RING_BUFFER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t *buf;
    uint32_t mask;          /* capacity - 1 */
    volatile uint32_t head; /* written only by the producer */
    volatile uint32_t tail; /* written only by the consumer */
} ring_buffer_t;

/* Returns false if a pointer is NULL or capacity is not a power of two. */
bool     rb_init(ring_buffer_t *rb, uint8_t *storage, uint32_t capacity);
/* Consumer side: discards everything currently queued. */
void     rb_clear(ring_buffer_t *rb);
uint32_t rb_capacity(const ring_buffer_t *rb);
uint32_t rb_count(const ring_buffer_t *rb);
uint32_t rb_free(const ring_buffer_t *rb);
bool     rb_is_empty(const ring_buffer_t *rb);
bool     rb_is_full(const ring_buffer_t *rb);
bool     rb_put(ring_buffer_t *rb, uint8_t byte);
bool     rb_get(ring_buffer_t *rb, uint8_t *byte);
bool     rb_peek(const ring_buffer_t *rb, uint8_t *byte);
/* Copies as much as fits / is available; returns the number of bytes moved. */
uint32_t rb_write(ring_buffer_t *rb, const void *data, uint32_t len);
uint32_t rb_read(ring_buffer_t *rb, void *data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* APP_RING_BUFFER_H */
