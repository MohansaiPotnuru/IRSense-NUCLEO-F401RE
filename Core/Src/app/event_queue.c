/**
 * @file    event_queue.c
 * @brief   Fixed-size SPSC queue of timestamped events.
 */
#include "app/event_queue.h"
#include "app/util.h"

#include <string.h>

_Static_assert((EVENT_QUEUE_CAPACITY & (EVENT_QUEUE_CAPACITY - 1u)) == 0u,
               "EVENT_QUEUE_CAPACITY must be a power of two");

#define EQ_MASK (EVENT_QUEUE_CAPACITY - 1u)

void eq_init(event_queue_t *q)
{
    memset(q, 0, sizeof(*q));
}

bool eq_push(event_queue_t *q, const event_t *e)
{
    uint32_t head = q->head;
    uint32_t used = head - q->tail;
    if (used >= EVENT_QUEUE_CAPACITY) {
        q->dropped++;
        return false;
    }
    q->items[head & EQ_MASK] = *e;
    COMPILER_BARRIER();
    q->head = head + 1u;
    if (used + 1u > q->high_water) {
        q->high_water = used + 1u;
    }
    return true;
}

bool eq_pop(event_queue_t *q, event_t *e)
{
    uint32_t tail = q->tail;
    if (q->head == tail) {
        return false;
    }
    *e = q->items[tail & EQ_MASK];
    COMPILER_BARRIER();
    q->tail = tail + 1u;
    return true;
}

uint32_t eq_count(const event_queue_t *q)
{
    return q->head - q->tail;
}
