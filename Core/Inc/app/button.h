/**
 * @file    button.h
 * @brief   Push-button debouncer and gesture detector.
 *
 * Sample the raw "pressed" state every few ms and call button_update().
 * Gestures:
 *   BTN_SHORT  - press + release, no second press within double_ms
 *   BTN_DOUBLE - two short presses within double_ms
 *   BTN_LONG   - held for long_ms (fires while still held, once)
 */
#ifndef APP_BUTTON_H
#define APP_BUTTON_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BTN_NONE = 0,
    BTN_SHORT,
    BTN_DOUBLE,
    BTN_LONG,
} btn_event_t;

typedef struct {
    uint16_t debounce_ms;
    uint16_t long_ms;
    uint16_t double_ms;
    bool     last_raw;
    bool     pressed;      /* debounced */
    bool     long_fired;
    uint8_t  clicks;
    uint32_t raw_since_ms;
    uint32_t press_ms;
    uint32_t release_ms;
} button_t;

void        button_init(button_t *b, uint16_t debounce_ms, uint16_t long_ms, uint16_t double_ms);
btn_event_t button_update(button_t *b, bool raw_pressed, uint32_t now_ms);
const char *button_event_name(btn_event_t e);

#ifdef __cplusplus
}
#endif

#endif /* APP_BUTTON_H */
