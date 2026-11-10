/**
 * @file    stats.c
 * @brief   Object statistics: totals, throughput and timing distributions.
 */
#include "app/stats.h"

#include <string.h>

void summary_reset(summary_t *s)
{
    s->n   = 0u;
    s->min = UINT32_MAX;
    s->max = 0u;
    s->sum = 0u;
}

void summary_add(summary_t *s, uint32_t v)
{
    s->n++;
    s->sum += v;
    if (v < s->min) {
        s->min = v;
    }
    if (v > s->max) {
        s->max = v;
    }
}

uint32_t summary_avg(const summary_t *s)
{
    if (s->n == 0u) {
        return 0u;
    }
    return (uint32_t)((s->sum + s->n / 2u) / s->n);
}

void stats_init(stats_t *s, uint32_t now_ms)
{
    memset(s, 0, sizeof(*s));
    summary_reset(&s->width_us);
    summary_reset(&s->gap_us);
    s->bucket_start_ms = now_ms;
}

void stats_advance(stats_t *s, uint32_t now_ms)
{
    uint32_t delta = now_ms - s->bucket_start_ms;
    if (delta < STATS_BUCKET_MS) {
        return;
    }
    if (delta >= STATS_BUCKET_MS * STATS_RATE_BUCKETS) {
        /* Idle for over a minute: the whole window is stale. */
        memset(s->bucket, 0, sizeof(s->bucket));
        s->bucket_start_ms = now_ms - (delta % STATS_BUCKET_MS);
        return;
    }
    while (delta >= STATS_BUCKET_MS) {
        s->bucket_idx = (s->bucket_idx + 1u) % STATS_RATE_BUCKETS;
        s->bucket[s->bucket_idx] = 0u;
        s->bucket_start_ms += STATS_BUCKET_MS;
        delta -= STATS_BUCKET_MS;
    }
}

uint32_t stats_add_object(stats_t *s, uint32_t start_us, uint32_t width_us, uint32_t now_ms)
{
    uint32_t gap = 0u;

    stats_advance(s, now_ms);
    s->total++;
    summary_add(&s->width_us, width_us);

    if (s->have_last_start && (int32_t)(start_us - s->last_start_us) > 0) {
        gap = start_us - s->last_start_us;
        summary_add(&s->gap_us, gap);
    }
    s->last_start_us   = start_us;
    s->have_last_start = true;

    if (s->bucket[s->bucket_idx] < UINT16_MAX) {
        s->bucket[s->bucket_idx]++;
    }
    uint32_t rate = stats_rate_per_min(s);
    if (rate > s->peak_rate) {
        s->peak_rate = rate;
    }
    return gap;
}

void stats_add_rejected(stats_t *s)
{
    s->rejected++;
}

uint32_t stats_rate_per_min(const stats_t *s)
{
    uint32_t sum = 0u;
    for (uint32_t i = 0u; i < STATS_RATE_BUCKETS; i++) {
        sum += s->bucket[i];
    }
    return sum;
}
