/**
 * @file    led_pattern.h
 * @brief   Time-based LED pattern sequencer with a short "flash" overlay.
 *
 * led_level() is a pure function of time, so the LED can be refreshed from
 * any periodic task. led_flash() briefly inverts the pattern (used as a
 * "blip" for every counted object).
 */
#ifndef APP_LED_PATTERN_H
#define APP_LED_PATTERN_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LED_OFF = 0,
    LED_ON,
    LED_SLOW_BLINK,   /* 1 Hz, 50 %           */
    LED_FAST_BLINK,   /* 5 Hz, 50 %  (alarm)  */
    LED_HEARTBEAT,    /* two quick pulses / s */
    LED_DOUBLE_FLASH, /* two flashes / 2 s    */
    LED_PATTERN_NUM
} led_pattern_t;

typedef struct {
    led_pattern_t pattern;
    uint32_t      start_ms;
    uint32_t      flash_until_ms;
    bool          flash_active;
} led_t;

void          led_init(led_t *led, uint32_t now_ms);
/* Changing to a different pattern restarts its phase; same pattern is a no-op. */
void          led_set(led_t *led, led_pattern_t pattern, uint32_t now_ms);
void          led_flash(led_t *led, uint32_t now_ms, uint32_t duration_ms);
bool          led_level(led_t *led, uint32_t now_ms);
const char   *led_pattern_name(led_pattern_t p);

#ifdef __cplusplus
}
#endif

#endif /* APP_LED_PATTERN_H */
