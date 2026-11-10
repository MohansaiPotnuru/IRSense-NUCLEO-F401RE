/**
 * @file    util.h
 * @brief   Small helpers shared by the portable application modules.
 *
 * Everything under Core/{Inc,Src}/app is hardware independent: it does not
 * include HAL or CMSIS headers, so it builds and is unit tested on a PC
 * (see tests/).
 */
#ifndef APP_UTIL_H
#define APP_UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/* Stops the compiler reordering memory accesses across this point. On a
 * single-core Cortex-M this is enough to publish data to/from an ISR. */
#define COMPILER_BARRIER() __asm__ volatile("" ::: "memory")

static inline bool is_pow2_u32(uint32_t x)
{
    return (x != 0u) && ((x & (x - 1u)) == 0u);
}

/* Wrap-safe "has `now` reached `deadline`" for free-running 32-bit clocks.
 * Valid while the two values are less than 2^31 ticks apart. */
static inline bool time_reached(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

static inline uint32_t min_u32(uint32_t a, uint32_t b) { return a < b ? a : b; }
static inline uint32_t max_u32(uint32_t a, uint32_t b) { return a > b ? a : b; }

/* Case-insensitive ASCII string compare: true when equal. */
bool str_ieq(const char *a, const char *b);

#ifdef __cplusplus
}
#endif

#endif /* APP_UTIL_H */
