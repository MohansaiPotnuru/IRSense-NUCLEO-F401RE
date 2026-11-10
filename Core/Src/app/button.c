/**
 * @file    button.c
 * @brief   Push-button debouncer and gesture detector.
 */
#include "app/button.h"

#include <string.h>

void button_init(button_t *b, uint16_t debounce_ms, uint16_t long_ms, uint16_t double_ms)
{
    memset(b, 0, sizeof(*b));
    b->debounce_ms = debounce_ms;
    b->long_ms     = long_ms;
    b->double_ms   = double_ms;
}

btn_event_t button_update(button_t *b, bool raw_pressed, uint32_t now_ms)
{
    if (raw_pressed != b->last_raw) {
        b->last_raw     = raw_pressed;
        b->raw_since_ms = now_ms;
    }

    /* Debounce: accept the raw level once it has been stable long enough. */
    if (raw_pressed != b->pressed && (now_ms - b->raw_since_ms) >= b->debounce_ms) {
        b->pressed = raw_pressed;
        if (b->pressed) {
            b->press_ms   = now_ms;
            b->long_fired = false;
        } else if (b->long_fired) {
            b->clicks = 0u; /* release after a long press: already reported */
        } else {
            b->clicks++;
            b->release_ms = now_ms;
            if (b->clicks >= 2u) {
                b->clicks = 0u;
                return BTN_DOUBLE;
            }
        }
    }

    if (b->pressed && !b->long_fired && (now_ms - b->press_ms) >= b->long_ms) {
        b->long_fired = true;
        b->clicks     = 0u;
        return BTN_LONG;
    }

    if (!b->pressed && b->clicks == 1u && (now_ms - b->release_ms) >= b->double_ms) {
        b->clicks = 0u;
        return BTN_SHORT;
    }

    return BTN_NONE;
}

const char *button_event_name(btn_event_t e)
{
    switch (e) {
    case BTN_SHORT:  return "short";
    case BTN_DOUBLE: return "double";
    case BTN_LONG:   return "long";
    case BTN_NONE:
    default:         return "none";
    }
}
