/**
 * @file    app.h
 * @brief   IRSense application logic (hardware independent).
 *
 * Inputs : filtered beam edges, button gestures, time.
 * Outputs: LED level, alarm flags, statistics and app_event_t records sent
 *          through app_port_t (the firmware prints them as $-records).
 *
 * Modes
 *   COUNTER  count objects, raise BATCH every `batch` objects
 *   MONITOR  measure only (width / gap / rate), LED slow blink
 *   GUARD    intrusion detector: after a 5 s arming delay any beam break
 *            latches an INTRUSION alarm until acknowledged
 *
 * Alarms (bit flags)
 *   JAM        beam blocked longer than jam_ms (clears when unblocked)
 *   RATE       rate above rate_alarm (clears below 90 % of it)
 *   INTRUSION  beam broken while GUARD is armed (momentary)
 *   OVERFLOW   edge events were lost (momentary)
 *   Every alarm also latches; `ack` / long press clears latched alarms
 *   that are no longer active.
 */
#ifndef APP_APP_H
#define APP_APP_H

#include <stdbool.h>
#include <stdint.h>

#include "app/button.h"
#include "app/config.h"
#include "app/led_pattern.h"
#include "app/stats.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_ARM_DELAY_MS   5000u
#define APP_BATCH_SHOW_MS  1500u
#define APP_OBJECT_BLIP_MS 40u

typedef enum {
    MODE_COUNTER = 0,
    MODE_MONITOR = 1,
    MODE_GUARD   = 2,
    APP_MODE_NUM
} app_mode_t;

typedef enum {
    GUARD_DISARMED = 0,
    GUARD_ARMING   = 1,
    GUARD_ARMED    = 2,
} guard_state_t;

typedef enum {
    ALARM_JAM       = 1u << 0,
    ALARM_RATE      = 1u << 1,
    ALARM_INTRUSION = 1u << 2,
    ALARM_OVERFLOW  = 1u << 3,
} alarm_flag_t;

typedef enum {
    APP_EVT_OBJECT,      /* a=seq b=width_us c=gap_us d=total       */
    APP_EVT_REJECT,      /* a=width_us                              */
    APP_EVT_BEAM,        /* a=1 blocked / 0 clear                   */
    APP_EVT_BATCH,       /* a=batches done b=target                 */
    APP_EVT_ALARM_SET,   /* a=flag(s)                               */
    APP_EVT_ALARM_CLEAR, /* a=flag(s)                               */
    APP_EVT_MODE,        /* a=mode                                  */
    APP_EVT_PAUSE,       /* a=1 paused / 0 running                  */
    APP_EVT_GUARD,       /* a=guard_state_t                         */
    APP_EVT_RESET,       /* counters reset                          */
} app_event_type_t;

typedef struct {
    app_event_type_t type;
    uint32_t         time_ms;
    uint32_t         a, b, c, d;
} app_event_t;

typedef struct {
    void (*emit)(void *ctx, const app_event_t *e);
    void  *ctx;
} app_port_t;

typedef struct {
    const config_t *cfg;
    app_port_t      port;

    app_mode_t      mode;
    bool            paused;
    guard_state_t   guard;
    uint32_t        arm_at_ms;

    bool            beam_blocked;
    bool            block_valid; /* false if the start of the block is unknown */
    uint32_t        block_start_us;
    uint32_t        block_start_ms;

    uint32_t        alarms;  /* currently active */
    uint32_t        latched; /* waiting for ack  */

    uint32_t        seq;
    uint32_t        batch_count;
    uint32_t        batches_done;
    bool            batch_show;
    uint32_t        batch_show_until_ms;

    stats_t         stats;
    led_t           led;
} app_t;

void app_init(app_t *app, const config_t *cfg, const app_port_t *port,
              bool beam_blocked, uint32_t now_ms);

/* Inputs */
void app_on_beam(app_t *app, bool blocked, uint32_t edge_us, uint32_t now_ms);
void app_on_button(app_t *app, btn_event_t ev, uint32_t now_ms);
void app_tick(app_t *app, uint32_t now_ms);
/* Re-sync the beam state without counting (e.g. after polarity change). */
void app_resync_beam(app_t *app, bool blocked, uint32_t now_ms);

/* Commands */
void     app_set_mode(app_t *app, app_mode_t mode, uint32_t now_ms);
void     app_set_paused(app_t *app, bool paused, uint32_t now_ms);
void     app_set_armed(app_t *app, bool armed, uint32_t now_ms);
uint32_t app_ack(app_t *app, uint32_t now_ms); /* returns flags cleared */
void     app_reset_counters(app_t *app, uint32_t now_ms);
void     app_raise_alarm(app_t *app, uint32_t flag, bool momentary, uint32_t now_ms);
/* Injects `count` synthetic objects ending at end_us. False if the beam is blocked. */
bool     app_simulate(app_t *app, uint32_t count, uint32_t width_us, uint32_t gap_us,
                      uint32_t end_us, uint32_t now_ms);

/* Outputs */
bool app_led(app_t *app, uint32_t now_ms);

/* Names */
const char *app_mode_name(app_mode_t mode);
bool        app_mode_parse(const char *s, app_mode_t *out);
const char *app_guard_name(guard_state_t g);
/* Writes e.g. "JAM|RATE" (or "none") into buf; returns buf. */
const char *app_alarm_names(uint32_t flags, char *buf, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* APP_APP_H */
