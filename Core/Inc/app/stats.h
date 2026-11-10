/**
 * @file    stats.h
 * @brief   Object statistics: totals, throughput and timing distributions.
 *
 * - width: how long each object blocked the beam (µs)
 * - gap:   time between the leading edges of consecutive objects (µs)
 * - rate:  objects in the last 60 s, from 60 one-second buckets
 */
#ifndef APP_STATS_H
#define APP_STATS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STATS_RATE_BUCKETS 60u
#define STATS_BUCKET_MS    1000u

typedef struct {
    uint32_t n;
    uint32_t min;
    uint32_t max;
    uint64_t sum;
} summary_t;

void     summary_reset(summary_t *s);
void     summary_add(summary_t *s, uint32_t v);
uint32_t summary_avg(const summary_t *s); /* 0 when empty */

typedef struct {
    uint32_t  total;
    uint32_t  rejected;
    summary_t width_us;
    summary_t gap_us;
    uint32_t  last_start_us;
    bool      have_last_start;
    uint16_t  bucket[STATS_RATE_BUCKETS];
    uint32_t  bucket_idx;
    uint32_t  bucket_start_ms;
    uint32_t  peak_rate;
} stats_t;

void     stats_init(stats_t *s, uint32_t now_ms);
/* Rolls the rate window forward to now_ms. */
void     stats_advance(stats_t *s, uint32_t now_ms);
/* Records an accepted object; returns the gap to the previous one (0 if none). */
uint32_t stats_add_object(stats_t *s, uint32_t start_us, uint32_t width_us, uint32_t now_ms);
void     stats_add_rejected(stats_t *s);
uint32_t stats_rate_per_min(const stats_t *s);

#ifdef __cplusplus
}
#endif

#endif /* APP_STATS_H */
