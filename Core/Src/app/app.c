/**
 * @file    app.c
 * @brief   IRSense application logic (hardware independent).
 */
#include "app/app.h"
#include "app/log.h"
#include "app/util.h"

#include <stdio.h>
#include <string.h>

#define TAG "app"

static const char *const k_mode_names[APP_MODE_NUM] = { "counter", "monitor", "guard" };

static void emit(app_t *app, app_event_type_t type, uint32_t now_ms,
                 uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    if (app->port.emit == NULL) {
        return;
    }
    app_event_t e = { type, now_ms, a, b, c, d };
    app->port.emit(app->port.ctx, &e);
}

void app_init(app_t *app, const config_t *cfg, const app_port_t *port,
              bool beam_blocked, uint32_t now_ms)
{
    memset(app, 0, sizeof(*app));
    app->cfg = cfg;
    if (port != NULL) {
        app->port = *port;
    }
    app->beam_blocked   = beam_blocked;
    app->block_valid    = false;
    app->block_start_ms = now_ms;
    stats_init(&app->stats, now_ms);
    led_init(&app->led, now_ms);

    app_mode_t mode = (cfg->boot_mode < (uint32_t)APP_MODE_NUM) ? (app_mode_t)cfg->boot_mode
                                                                : MODE_COUNTER;
    app->mode = MODE_COUNTER;
    if (mode != MODE_COUNTER) {
        app_set_mode(app, mode, now_ms);
    }
}

/* --------------------------------------------------------------------------
 * Alarms
 * ------------------------------------------------------------------------ */

void app_raise_alarm(app_t *app, uint32_t flag, bool momentary, uint32_t now_ms)
{
    bool already = ((app->alarms | app->latched) & flag) != 0u;
    if (!momentary) {
        app->alarms |= flag;
    }
    app->latched |= flag;
    if (!already) {
        char names[48];
        LOG_W(TAG, "ALARM %s", app_alarm_names(flag, names, sizeof(names)));
        emit(app, APP_EVT_ALARM_SET, now_ms, flag, 0u, 0u, 0u);
    }
}

static void clear_active(app_t *app, uint32_t flag)
{
    app->alarms &= ~flag; /* stays latched until acknowledged */
}

uint32_t app_ack(app_t *app, uint32_t now_ms)
{
    uint32_t cleared = app->latched & ~app->alarms;
    app->latched &= app->alarms;
    if (cleared != 0u) {
        char names[48];
        LOG_I(TAG, "acknowledged %s", app_alarm_names(cleared, names, sizeof(names)));
        emit(app, APP_EVT_ALARM_CLEAR, now_ms, cleared, 0u, 0u, 0u);
    }
    return cleared;
}

/* --------------------------------------------------------------------------
 * Commands
 * ------------------------------------------------------------------------ */

static void set_guard(app_t *app, guard_state_t g, uint32_t now_ms)
{
    if (app->guard == g) {
        return;
    }
    app->guard = g;
    if (g == GUARD_ARMING) {
        app->arm_at_ms = now_ms + APP_ARM_DELAY_MS;
    }
    LOG_I(TAG, "guard %s", app_guard_name(g));
    emit(app, APP_EVT_GUARD, now_ms, (uint32_t)g, 0u, 0u, 0u);
}

void app_set_mode(app_t *app, app_mode_t mode, uint32_t now_ms)
{
    if (mode >= APP_MODE_NUM || mode == app->mode) {
        return;
    }
    app->mode = mode;
    set_guard(app, (mode == MODE_GUARD) ? GUARD_ARMING : GUARD_DISARMED, now_ms);
    LOG_I(TAG, "mode %s", app_mode_name(mode));
    emit(app, APP_EVT_MODE, now_ms, (uint32_t)mode, 0u, 0u, 0u);
}

void app_set_paused(app_t *app, bool paused, uint32_t now_ms)
{
    if (app->paused == paused) {
        return;
    }
    app->paused = paused;
    LOG_I(TAG, "%s", paused ? "paused" : "resumed");
    emit(app, APP_EVT_PAUSE, now_ms, paused ? 1u : 0u, 0u, 0u, 0u);
}

void app_set_armed(app_t *app, bool armed, uint32_t now_ms)
{
    if (app->mode != MODE_GUARD) {
        return;
    }
    if (!armed) {
        set_guard(app, GUARD_DISARMED, now_ms);
    } else if (app->guard == GUARD_DISARMED) {
        set_guard(app, GUARD_ARMING, now_ms);
    }
}

void app_reset_counters(app_t *app, uint32_t now_ms)
{
    stats_init(&app->stats, now_ms);
    app->seq          = 0u;
    app->batch_count  = 0u;
    app->batches_done = 0u;
    app->batch_show   = false;
    LOG_I(TAG, "counters reset");
    emit(app, APP_EVT_RESET, now_ms, 0u, 0u, 0u, 0u);
}

/* --------------------------------------------------------------------------
 * Inputs
 * ------------------------------------------------------------------------ */

static void object_passed(app_t *app, uint32_t start_us, uint32_t width_us, uint32_t now_ms)
{
    if (width_us < app->cfg->min_width_us) {
        stats_add_rejected(&app->stats);
        LOG_D(TAG, "rejected %lu us", (unsigned long)width_us);
        emit(app, APP_EVT_REJECT, now_ms, width_us, 0u, 0u, 0u);
        return;
    }

    uint32_t gap = stats_add_object(&app->stats, start_us, width_us, now_ms);
    app->seq++;
    led_flash(&app->led, now_ms, APP_OBJECT_BLIP_MS);
    LOG_D(TAG, "object #%lu width %lu us gap %lu us", (unsigned long)app->seq,
          (unsigned long)width_us, (unsigned long)gap);
    emit(app, APP_EVT_OBJECT, now_ms, app->seq, width_us, gap, app->stats.total);

    if (app->mode == MODE_COUNTER && app->cfg->batch_target > 0u) {
        app->batch_count++;
        if (app->batch_count >= app->cfg->batch_target) {
            app->batch_count = 0u;
            app->batches_done++;
            app->batch_show          = true;
            app->batch_show_until_ms = now_ms + APP_BATCH_SHOW_MS;
            LOG_I(TAG, "batch %lu complete (%lu objects)", (unsigned long)app->batches_done,
                  (unsigned long)app->cfg->batch_target);
            emit(app, APP_EVT_BATCH, now_ms, app->batches_done, app->cfg->batch_target, 0u, 0u);
        }
    }
}

void app_on_beam(app_t *app, bool blocked, uint32_t edge_us, uint32_t now_ms)
{
    if (blocked == app->beam_blocked) {
        return;
    }
    app->beam_blocked = blocked;
    emit(app, APP_EVT_BEAM, now_ms, blocked ? 1u : 0u, 0u, 0u, 0u);

    if (blocked) {
        app->block_valid    = true;
        app->block_start_us = edge_us;
        app->block_start_ms = now_ms;
        if (app->mode == MODE_GUARD && app->guard == GUARD_ARMED && !app->paused) {
            app_raise_alarm(app, ALARM_INTRUSION, true, now_ms);
        }
        return;
    }

    clear_active(app, ALARM_JAM);
    if (!app->block_valid || app->paused) {
        return;
    }
    object_passed(app, app->block_start_us, edge_us - app->block_start_us, now_ms);
}

void app_resync_beam(app_t *app, bool blocked, uint32_t now_ms)
{
    app->beam_blocked   = blocked;
    app->block_valid    = false;
    app->block_start_ms = now_ms;
    if (!blocked) {
        clear_active(app, ALARM_JAM);
    }
}

void app_on_button(app_t *app, btn_event_t ev, uint32_t now_ms)
{
    switch (ev) {
    case BTN_SHORT:
        app_set_mode(app, (app_mode_t)(((uint32_t)app->mode + 1u) % (uint32_t)APP_MODE_NUM),
                     now_ms);
        break;
    case BTN_DOUBLE:
        app_reset_counters(app, now_ms);
        break;
    case BTN_LONG:
        if ((app->alarms | app->latched) != 0u) {
            (void)app_ack(app, now_ms);
        } else if (app->mode == MODE_GUARD) {
            app_set_armed(app, app->guard == GUARD_DISARMED, now_ms);
        } else {
            app_set_paused(app, !app->paused, now_ms);
        }
        break;
    case BTN_NONE:
    default:
        break;
    }
}

void app_tick(app_t *app, uint32_t now_ms)
{
    const config_t *cfg = app->cfg;

    stats_advance(&app->stats, now_ms);

    if (app->guard == GUARD_ARMING && time_reached(now_ms, app->arm_at_ms)) {
        set_guard(app, GUARD_ARMED, now_ms);
    }

    if (app->beam_blocked && cfg->jam_timeout_ms > 0u && (app->alarms & ALARM_JAM) == 0u &&
        (now_ms - app->block_start_ms) >= cfg->jam_timeout_ms) {
        app_raise_alarm(app, ALARM_JAM, false, now_ms);
    }

    if (cfg->rate_alarm_per_min > 0u) {
        uint32_t rate = stats_rate_per_min(&app->stats);
        if ((app->alarms & ALARM_RATE) == 0u) {
            if (rate > cfg->rate_alarm_per_min) {
                app_raise_alarm(app, ALARM_RATE, false, now_ms);
            }
        } else if ((uint64_t)rate * 10u < (uint64_t)cfg->rate_alarm_per_min * 9u) {
            clear_active(app, ALARM_RATE);
        }
    } else {
        clear_active(app, ALARM_RATE);
    }

    if (app->batch_show && time_reached(now_ms, app->batch_show_until_ms)) {
        app->batch_show = false;
    }

    led_pattern_t p;
    if ((app->alarms | app->latched) != 0u) {
        p = LED_FAST_BLINK;
    } else if (app->batch_show) {
        p = LED_ON;
    } else if (app->paused) {
        p = LED_OFF;
    } else if (app->mode == MODE_COUNTER) {
        p = LED_HEARTBEAT;
    } else if (app->mode == MODE_MONITOR) {
        p = LED_SLOW_BLINK;
    } else {
        p = (app->guard == GUARD_ARMED)    ? LED_ON
            : (app->guard == GUARD_ARMING) ? LED_DOUBLE_FLASH
                                           : LED_OFF;
    }
    led_set(&app->led, p, now_ms);
}

bool app_simulate(app_t *app, uint32_t count, uint32_t width_us, uint32_t gap_us,
                  uint32_t end_us, uint32_t now_ms)
{
    if (app->beam_blocked || count == 0u) {
        return !app->beam_blocked;
    }
    /* Place the synthetic objects in the past so later real gaps stay positive. */
    uint32_t t = end_us - count * (width_us + gap_us);
    for (uint32_t i = 0u; i < count; i++) {
        app_on_beam(app, true, t, now_ms);
        t += width_us;
        app_on_beam(app, false, t, now_ms);
        t += gap_us;
    }
    return true;
}

/* --------------------------------------------------------------------------
 * Outputs / names
 * ------------------------------------------------------------------------ */

bool app_led(app_t *app, uint32_t now_ms)
{
    return led_level(&app->led, now_ms);
}

const char *app_mode_name(app_mode_t mode)
{
    return (mode < APP_MODE_NUM) ? k_mode_names[mode] : "?";
}

bool app_mode_parse(const char *s, app_mode_t *out)
{
    for (unsigned i = 0u; i < (unsigned)APP_MODE_NUM; i++) {
        if (str_ieq(s, k_mode_names[i])) {
            *out = (app_mode_t)i;
            return true;
        }
    }
    return false;
}

const char *app_guard_name(guard_state_t g)
{
    switch (g) {
    case GUARD_DISARMED: return "disarmed";
    case GUARD_ARMING:   return "arming";
    case GUARD_ARMED:    return "armed";
    default:             return "?";
    }
}

const char *app_alarm_names(uint32_t flags, char *buf, uint32_t len)
{
    static const struct {
        uint32_t    flag;
        const char *name;
    } k_alarms[] = {
        { ALARM_JAM, "JAM" },
        { ALARM_RATE, "RATE" },
        { ALARM_INTRUSION, "INTRUSION" },
        { ALARM_OVERFLOW, "OVERFLOW" },
    };

    if (len == 0u) {
        return buf;
    }
    buf[0] = '\0';
    size_t used = 0u;
    for (size_t i = 0u; i < ARRAY_SIZE(k_alarms); i++) {
        if ((flags & k_alarms[i].flag) == 0u) {
            continue;
        }
        int n = snprintf(&buf[used], len - used, "%s%s", (used > 0u) ? "|" : "", k_alarms[i].name);
        if (n < 0 || (size_t)n >= len - used) {
            break;
        }
        used += (size_t)n;
    }
    if (used == 0u) {
        snprintf(buf, len, "none");
    }
    return buf;
}
