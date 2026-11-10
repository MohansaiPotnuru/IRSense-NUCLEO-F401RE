/**
 * @file    scheduler.h
 * @brief   Cooperative periodic task scheduler with run-time profiling.
 *
 * Tasks run to completion from the main loop. A task that falls behind by
 * more than a whole period is counted as an overrun and re-phased instead
 * of being run several times in a row to "catch up".
 */
#ifndef APP_SCHEDULER_H
#define APP_SCHEDULER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*task_fn_t)(void *ctx, uint32_t now_ms);

typedef struct {
    const char *name;
    task_fn_t   fn;
    void       *ctx;
    uint32_t    period_ms;
    /* runtime state, managed by the scheduler */
    uint32_t    next_ms;
    uint32_t    runs;
    uint32_t    overruns;
    uint32_t    last_us;
    uint32_t    max_us;
} task_t;

typedef struct {
    task_t   *tasks;
    size_t    count;
    uint32_t (*clock_us)(void); /* optional, for execution-time stats */
} scheduler_t;

void     sched_init(scheduler_t *s, task_t *tasks, size_t count,
                    uint32_t (*clock_us)(void), uint32_t now_ms);
/* Runs every due task once; returns how many ran. */
unsigned sched_run(scheduler_t *s, uint32_t now_ms);
void     sched_reset_stats(scheduler_t *s);

#ifdef __cplusplus
}
#endif

#endif /* APP_SCHEDULER_H */
