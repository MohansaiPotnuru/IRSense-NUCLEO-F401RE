/**
 * @file    edge_filter.h
 * @brief   Timestamp-based glitch filter for a digital input.
 *
 * Raw edges (with µs timestamps captured in the ISR) go in; a level change
 * is accepted only once the new level has been stable for `stable_us`.
 * The reported edge time is the time of the raw edge that *started* the
 * stable period, so pulse widths stay accurate to the µs despite the
 * filter delay.
 *
 *   raw:     ‾‾‾|_|‾|________________|‾‾‾‾‾‾‾‾‾‾
 *   stable:  ‾‾‾‾‾‾‾‾‾‾‾‾‾‾|__________________|‾‾‾  (reported at edge times)
 *                  ^ glitches shorter than stable_us are ignored
 */
#ifndef APP_EDGE_FILTER_H
#define APP_EDGE_FILTER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t stable_us;     /* required stability time; may be changed live */
    uint32_t raw_change_us; /* timestamp of the most recent raw edge */
    bool     stable_level;  /* filtered output */
    bool     raw_level;     /* latest raw level */
    bool     pending;       /* raw differs from stable, waiting it out */
    uint32_t glitches;      /* raw edges that never became stable */
} edge_filter_t;

void ef_init(edge_filter_t *f, bool initial_level, uint32_t stable_us);

/* Feed one raw edge (level after the edge, timestamp in µs). */
void ef_raw_edge(edge_filter_t *f, bool level, uint32_t t_us);

/* Call regularly. Returns true when the filtered level changes; then
 * *level is the new level and *edge_us the time it really changed. */
bool ef_poll(edge_filter_t *f, uint32_t now_us, bool *level, uint32_t *edge_us);

#ifdef __cplusplus
}
#endif

#endif /* APP_EDGE_FILTER_H */
