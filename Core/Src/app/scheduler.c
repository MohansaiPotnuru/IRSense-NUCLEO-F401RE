/**
 * @file    scheduler.c
 * @brief   Cooperative periodic task scheduler with run-time profiling.
 */
#include "app/scheduler.h"
#include "app/util.h"

void sched_init(scheduler_t *s, task_t *tasks, size_t count,
                uint32_t (*clock_us)(void), uint32_t now_ms)
{
    s->tasks    = tasks;
    s->count    = count;
    s->clock_us = clock_us;
    for (size_t i = 0u; i < count; i++) {
        tasks[i].next_ms = now_ms; /* first run immediately */
    }
    sched_reset_stats(s);
}

void sched_reset_stats(scheduler_t *s)
{
    for (size_t i = 0u; i < s->count; i++) {
        s->tasks[i].runs     = 0u;
        s->tasks[i].overruns = 0u;
        s->tasks[i].last_us  = 0u;
        s->tasks[i].max_us   = 0u;
    }
}

unsigned sched_run(scheduler_t *s, uint32_t now_ms)
{
    unsigned ran = 0u;

    for (size_t i = 0u; i < s->count; i++) {
        task_t *t = &s->tasks[i];
        if (t->fn == NULL || !time_reached(now_ms, t->next_ms)) {
            continue;
        }

        t->next_ms += t->period_ms;
        if (time_reached(now_ms, t->next_ms)) {
            /* Missed at least one whole period: re-phase from now. */
            t->overruns++;
            t->next_ms = now_ms + t->period_ms;
        }

        uint32_t t0 = (s->clock_us != NULL) ? s->clock_us() : 0u;
        t->fn(t->ctx, now_ms);
        if (s->clock_us != NULL) {
            t->last_us = s->clock_us() - t0;
            if (t->last_us > t->max_us) {
                t->max_us = t->last_us;
            }
        }
        t->runs++;
        ran++;
    }
    return ran;
}
