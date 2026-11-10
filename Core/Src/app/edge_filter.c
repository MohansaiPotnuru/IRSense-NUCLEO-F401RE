/**
 * @file    edge_filter.c
 * @brief   Timestamp-based glitch filter for a digital input.
 */
#include "app/edge_filter.h"

#include <stddef.h>

void ef_init(edge_filter_t *f, bool initial_level, uint32_t stable_us)
{
    f->stable_us     = stable_us;
    f->raw_change_us = 0u;
    f->stable_level  = initial_level;
    f->raw_level     = initial_level;
    f->pending       = false;
    f->glitches      = 0u;
}

void ef_raw_edge(edge_filter_t *f, bool level, uint32_t t_us)
{
    if (level == f->raw_level) {
        return; /* duplicate (the ISR may sample the same level twice) */
    }
    f->raw_level     = level;
    f->raw_change_us = t_us;

    if (level == f->stable_level) {
        /* Went back before it became stable: that was a glitch. */
        if (f->pending) {
            f->glitches++;
        }
        f->pending = false;
    } else {
        f->pending = true;
    }
}

bool ef_poll(edge_filter_t *f, uint32_t now_us, bool *level, uint32_t *edge_us)
{
    if (!f->pending) {
        return false;
    }
    int32_t age = (int32_t)(now_us - f->raw_change_us);
    if (age < 0 || (uint32_t)age < f->stable_us) {
        return false;
    }
    f->stable_level = f->raw_level;
    f->pending      = false;
    if (level != NULL) {
        *level = f->stable_level;
    }
    if (edge_us != NULL) {
        *edge_us = f->raw_change_us;
    }
    return true;
}
