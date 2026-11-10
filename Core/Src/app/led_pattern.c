/**
 * @file    led_pattern.c
 * @brief   Time-based LED pattern sequencer with a short "flash" overlay.
 */
#include "app/led_pattern.h"
#include "app/util.h"

typedef struct {
    uint16_t duration_ms;
    bool     on;
} led_step_t;

typedef struct {
    const char       *name;
    const led_step_t *steps;
    uint8_t           count;
} led_sequence_t;

static const led_step_t k_off[]    = { { 1000u, false } };
static const led_step_t k_on[]     = { { 1000u, true } };
static const led_step_t k_slow[]   = { { 500u, true }, { 500u, false } };
static const led_step_t k_fast[]   = { { 100u, true }, { 100u, false } };
static const led_step_t k_heart[]  = { { 80u, true }, { 120u, false }, { 80u, true }, { 720u, false } };
static const led_step_t k_double[] = { { 100u, true }, { 150u, false }, { 100u, true }, { 1650u, false } };

static const led_sequence_t k_sequences[LED_PATTERN_NUM] = {
    [LED_OFF]          = { "off",       k_off,    (uint8_t)ARRAY_SIZE(k_off) },
    [LED_ON]           = { "on",        k_on,     (uint8_t)ARRAY_SIZE(k_on) },
    [LED_SLOW_BLINK]   = { "slow",      k_slow,   (uint8_t)ARRAY_SIZE(k_slow) },
    [LED_FAST_BLINK]   = { "fast",      k_fast,   (uint8_t)ARRAY_SIZE(k_fast) },
    [LED_HEARTBEAT]    = { "heartbeat", k_heart,  (uint8_t)ARRAY_SIZE(k_heart) },
    [LED_DOUBLE_FLASH] = { "double",    k_double, (uint8_t)ARRAY_SIZE(k_double) },
};

void led_init(led_t *led, uint32_t now_ms)
{
    led->pattern        = LED_OFF;
    led->start_ms       = now_ms;
    led->flash_until_ms = now_ms;
    led->flash_active   = false;
}

void led_set(led_t *led, led_pattern_t pattern, uint32_t now_ms)
{
    if (pattern >= LED_PATTERN_NUM || pattern == led->pattern) {
        return;
    }
    led->pattern  = pattern;
    led->start_ms = now_ms;
}

void led_flash(led_t *led, uint32_t now_ms, uint32_t duration_ms)
{
    led->flash_active   = true;
    led->flash_until_ms = now_ms + duration_ms;
}

static bool sequence_level(const led_sequence_t *seq, uint32_t elapsed_ms)
{
    uint32_t period = 0u;
    for (uint8_t i = 0u; i < seq->count; i++) {
        period += seq->steps[i].duration_ms;
    }
    uint32_t t = elapsed_ms % period;
    for (uint8_t i = 0u; i < seq->count; i++) {
        if (t < seq->steps[i].duration_ms) {
            return seq->steps[i].on;
        }
        t -= seq->steps[i].duration_ms;
    }
    return false;
}

bool led_level(led_t *led, uint32_t now_ms)
{
    bool level = sequence_level(&k_sequences[led->pattern], now_ms - led->start_ms);
    if (led->flash_active) {
        if (time_reached(now_ms, led->flash_until_ms)) {
            led->flash_active = false;
        } else {
            level = !level;
        }
    }
    return level;
}

const char *led_pattern_name(led_pattern_t p)
{
    return (p < LED_PATTERN_NUM) ? k_sequences[p].name : "?";
}
