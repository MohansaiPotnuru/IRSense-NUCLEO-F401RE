/**
 * @file    event_queue.h
 * @brief   Fixed-size SPSC queue of timestamped events (ISR -> main loop).
 *
 * The EXTI interrupt pushes one event per IR edge; the main loop pops them.
 * When the queue is full the event is dropped and `dropped` is incremented
 * so the application can raise an OVERFLOW alarm.
 */
#ifndef APP_EVENT_QUEUE_H
#define APP_EVENT_QUEUE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EVENT_QUEUE_CAPACITY 64u /* must be a power of two */

typedef enum {
    EVT_NONE    = 0,
    EVT_IR_EDGE = 1, /* arg = pin level after the edge (0/1) */
} event_type_t;

typedef struct {
    uint32_t timestamp_us;
    uint16_t type;
    uint16_t arg;
} event_t;

typedef struct {
    event_t items[EVENT_QUEUE_CAPACITY];
    volatile uint32_t head;    /* producer */
    volatile uint32_t tail;    /* consumer */
    volatile uint32_t dropped; /* producer: events lost because full */
    volatile uint32_t high_water;
} event_queue_t;

void     eq_init(event_queue_t *q);
bool     eq_push(event_queue_t *q, const event_t *e); /* producer */
bool     eq_pop(event_queue_t *q, event_t *e);        /* consumer */
uint32_t eq_count(const event_queue_t *q);

#ifdef __cplusplus
}
#endif

#endif /* APP_EVENT_QUEUE_H */
