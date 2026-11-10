/**
 * @file    firmware.c
 * @brief   Glue between the board (bsp) and the portable application (app).
 *
 * Main loop (fw_poll):
 *   1. feed received UART bytes to the CLI
 *   2. drain IR edge events from the ISR queue into the glitch filter
 *   3. hand filtered beam changes to the application
 *   4. run due scheduler tasks (button, app logic, LED, health, report)
 *   5. sleep until the next interrupt (SysTick wakes us every 1 ms)
 */
#include "bsp/firmware.h"
#include "bsp/bsp.h"
#include "main.h"

#include "app/app.h"
#include "app/app_cli.h"
#include "app/button.h"
#include "app/config.h"
#include "app/config_store.h"
#include "app/edge_filter.h"
#include "app/event_queue.h"
#include "app/log.h"
#include "app/scheduler.h"
#include "app/util.h"
#include "app/version.h"

#include <stdio.h>

#define TAG "sys"

#define UART_BAUD            115200u
#define WATCHDOG_TIMEOUT_MS  8000u /* > worst-case flash sector erase */
#define BTN_DEBOUNCE_MS      30u
#define BTN_LONG_MS          800u
#define BTN_DOUBLE_MS        300u
#define REPORT_PERIOD_MS     60000u

static config_t      g_cfg;
static app_t         g_app;
static cli_t         g_cli;
static app_cli_ctx_t g_cli_ctx;
static event_queue_t g_ir_events;
static edge_filter_t g_ir_filter;
static button_t      g_button;
static scheduler_t   g_sched;
static uint32_t      g_last_active_low;
static uint32_t      g_reported_drops;
static uint32_t      g_reported_rx_ovf;

/* ---- Output sinks ---------------------------------------------------------- */

static void out_write(const char *s, size_t len)
{
    (void)bsp_uart_write(s, len);
    bsp_swo_write(s, len);
}

static bool level_to_blocked(bool level)
{
    return g_cfg.ir_active_low ? !level : level;
}

/* Prints application events as machine-readable $-records (see docs/CLI.md). */
static void on_app_event(void *ctx, const app_event_t *e)
{
    (void)ctx;
    if (!g_cfg.stream) {
        return;
    }
    char        line[96];
    char        names[48];
    int         n  = 0;
    unsigned long t = (unsigned long)e->time_ms;

    switch (e->type) {
    case APP_EVT_OBJECT:
        n = snprintf(line, sizeof(line), "$OBJ,%lu,%lu,%lu,%lu,%lu\r\n", t, (unsigned long)e->a,
                     (unsigned long)e->b, (unsigned long)e->c, (unsigned long)e->d);
        break;
    case APP_EVT_REJECT:
        n = snprintf(line, sizeof(line), "$REJ,%lu,%lu\r\n", t, (unsigned long)e->a);
        break;
    case APP_EVT_BEAM:
        n = snprintf(line, sizeof(line), "$BEAM,%lu,%lu\r\n", t, (unsigned long)e->a);
        break;
    case APP_EVT_BATCH:
        n = snprintf(line, sizeof(line), "$BATCH,%lu,%lu,%lu\r\n", t, (unsigned long)e->a,
                     (unsigned long)e->b);
        break;
    case APP_EVT_ALARM_SET:
    case APP_EVT_ALARM_CLEAR:
        n = snprintf(line, sizeof(line), "$ALARM,%lu,%s,%s\r\n", t,
                     (e->type == APP_EVT_ALARM_SET) ? "SET" : "CLEAR",
                     app_alarm_names(e->a, names, sizeof(names)));
        break;
    case APP_EVT_MODE:
        n = snprintf(line, sizeof(line), "$MODE,%lu,%s\r\n", t, app_mode_name((app_mode_t)e->a));
        break;
    case APP_EVT_PAUSE:
        n = snprintf(line, sizeof(line), "$PAUSE,%lu,%lu\r\n", t, (unsigned long)e->a);
        break;
    case APP_EVT_GUARD:
        n = snprintf(line, sizeof(line), "$GUARD,%lu,%s\r\n", t,
                     app_guard_name((guard_state_t)e->a));
        break;
    case APP_EVT_RESET:
        n = snprintf(line, sizeof(line), "$RESET,%lu\r\n", t);
        break;
    default:
        break;
    }
    if (n > 0) {
        (void)bsp_uart_write(line, min_u32((uint32_t)n, sizeof(line) - 1u));
    }
}

/* ---- Platform callbacks for the CLI --------------------------------------- */

static void print_sys(cli_t *cli)
{
    uint32_t uid[3];
    bsp_uid(uid);
    config_store_info_t info;
    config_t            tmp;
    cfg_store_status_t  st = config_store_load(bsp_config_flash(), &tmp, &info);

    cli_printf(cli, "firmware  : %s %s (%s)\r\n", FW_NAME, FW_VERSION, FW_GIT_HASH);
    cli_printf(cli, "board     : %s, SYSCLK %lu MHz\r\n", FW_BOARD,
               (unsigned long)(SystemCoreClock / 1000000u));
    cli_printf(cli, "uid       : %08lX-%08lX-%08lX\r\n", (unsigned long)uid[2],
               (unsigned long)uid[1], (unsigned long)uid[0]);
    cli_printf(cli, "reset     : %s\r\n", bsp_reset_cause());
    cli_printf(cli, "ir edges  : %lu raw, %lu glitches filtered\r\n",
               (unsigned long)bsp_ir_edge_count(), (unsigned long)g_ir_filter.glitches);
    cli_printf(cli, "ir queue  : %lu/%lu high water, %lu dropped\r\n",
               (unsigned long)g_ir_events.high_water, (unsigned long)EVENT_QUEUE_CAPACITY,
               (unsigned long)g_ir_events.dropped);
    cli_printf(cli, "uart      : %lu rx overflows, %lu tx bytes dropped\r\n",
               (unsigned long)bsp_uart_rx_overflows(), (unsigned long)bsp_uart_tx_dropped());
    cli_printf(cli, "config    : %s, %lu/%lu slots used, record #%lu\r\n",
               config_store_status_name(st), (unsigned long)info.slots_used,
               (unsigned long)info.slot_count, (unsigned long)info.last_seq);
    cli_printf(cli, "log lines : %lu\r\n", (unsigned long)log_lines_written());
}

static void print_tasks(cli_t *cli)
{
    cli_printf(cli, "%-8s %8s %10s %9s %8s %8s\r\n", "task", "period", "runs", "overruns",
               "last us", "max us");
    for (size_t i = 0u; i < g_sched.count; i++) {
        const task_t *t = &g_sched.tasks[i];
        cli_printf(cli, "%-8s %6lums %10lu %9lu %8lu %8lu\r\n", t->name,
                   (unsigned long)t->period_ms, (unsigned long)t->runs,
                   (unsigned long)t->overruns, (unsigned long)t->last_us,
                   (unsigned long)t->max_us);
    }
}

static void config_changed(void)
{
    g_ir_filter.stable_us = g_cfg.debounce_us;
    if (g_cfg.ir_active_low != g_last_active_low) {
        /* Polarity flipped: re-sync without counting a bogus object. */
        g_last_active_low = g_cfg.ir_active_low;
        app_resync_beam(&g_app, level_to_blocked(g_ir_filter.stable_level), bsp_time_ms());
    }
}

static const app_platform_t k_platform = {
    .now_ms         = bsp_time_ms,
    .now_us         = bsp_time_us,
    .print_sys      = print_sys,
    .print_tasks    = print_tasks,
    .reboot         = bsp_reboot,
    .config_changed = config_changed,
};

/* ---- Scheduler tasks ----------------------------------------------------- */

static void task_button(void *ctx, uint32_t now_ms)
{
    (void)ctx;
    btn_event_t ev = button_update(&g_button, bsp_button_pressed(), now_ms);
    if (ev != BTN_NONE) {
        LOG_D("btn", "%s press", button_event_name(ev));
        app_on_button(&g_app, ev, now_ms);
    }
}

static void task_app(void *ctx, uint32_t now_ms)
{
    (void)ctx;
    app_tick(&g_app, now_ms);
}

static void task_led(void *ctx, uint32_t now_ms)
{
    (void)ctx;
    bsp_led_write(app_led(&g_app, now_ms));
}

static void task_health(void *ctx, uint32_t now_ms)
{
    (void)ctx;
    bsp_wdg_kick();

    uint32_t drops = g_ir_events.dropped;
    if (drops != g_reported_drops) {
        g_reported_drops = drops;
        app_raise_alarm(&g_app, ALARM_OVERFLOW, true, now_ms);
    }
    uint32_t ovf = bsp_uart_rx_overflows();
    if (ovf != g_reported_rx_ovf) {
        g_reported_rx_ovf = ovf;
        LOG_W("uart", "rx overflow (%lu total)", (unsigned long)ovf);
    }

    /* If edges were lost the filter may disagree with the pin: re-sync it. */
    bool level = bsp_ir_level();
    if (eq_count(&g_ir_events) == 0u && level != g_ir_filter.raw_level) {
        ef_raw_edge(&g_ir_filter, level, bsp_time_us());
    }
}

static void task_report(void *ctx, uint32_t now_ms)
{
    (void)ctx;
    (void)now_ms;
    static bool s_first = true;
    if (s_first) {
        s_first = false; /* the scheduler runs every task once at start-up */
        return;
    }
    LOG_I("rpt", "objects %lu, rate %lu/min, batches %lu",
          (unsigned long)g_app.stats.total, (unsigned long)stats_rate_per_min(&g_app.stats),
          (unsigned long)g_app.batches_done);
}

static task_t g_tasks[] = {
    { .name = "button", .fn = task_button, .period_ms = 5u },
    { .name = "app",    .fn = task_app,    .period_ms = 10u },
    { .name = "led",    .fn = task_led,    .period_ms = 10u },
    { .name = "health", .fn = task_health, .period_ms = 100u },
    { .name = "report", .fn = task_report, .period_ms = REPORT_PERIOD_MS },
};

/* ---- IR processing --------------------------------------------------------- */

static void process_ir(void)
{
    event_t  e;
    uint32_t now_us;

    /* Read the clock only once the queue is empty. Any edge that arrives
     * later has a timestamp >= now_us, so the filter can't confirm a level
     * that actually ended before now_us. */
    for (;;) {
        while (eq_pop(&g_ir_events, &e)) {
            if (e.type == EVT_IR_EDGE) {
                ef_raw_edge(&g_ir_filter, e.arg != 0u, e.timestamp_us);
            }
        }
        now_us = bsp_time_us();
        if (eq_count(&g_ir_events) == 0u) {
            break;
        }
    }

    bool     level;
    uint32_t edge_us;
    if (ef_poll(&g_ir_filter, now_us, &level, &edge_us)) {
        app_on_beam(&g_app, level_to_blocked(level), edge_us, bsp_time_ms());
    }
}

/* ---- Public ---------------------------------------------------------------- */

void fw_init(void)
{
    bsp_reset_cause_capture();
    /* Keep the debug/SWO link alive while the core sleeps in __WFI(). */
    DBGMCU->CR |= DBGMCU_CR_DBG_SLEEP;
    bsp_time_init();
    bsp_uart_init(UART_BAUD);
    log_init(out_write, bsp_time_ms);

    config_set_defaults(&g_cfg);
    config_store_info_t info;
    cfg_store_status_t  st = config_store_load(bsp_config_flash(), &g_cfg, &info);
    log_set_level((log_level_t)g_cfg.log_level);
    g_last_active_low = g_cfg.ir_active_low;

    out_write("\r\n", 2u);
    LOG_I(TAG, "%s %s (%s) on %s", FW_NAME, FW_VERSION, FW_GIT_HASH, FW_BOARD);
    LOG_I(TAG, "reset cause: %s", bsp_reset_cause());
    if (bsp_reset_was_watchdog()) {
        LOG_W(TAG, "recovered from a watchdog reset");
    }
    if (st == CFG_STORE_OK) {
        LOG_I(TAG, "config loaded (record #%lu)", (unsigned long)info.last_seq);
    } else {
        LOG_I(TAG, "using default config (%s)", config_store_status_name(st));
    }

    eq_init(&g_ir_events);
    bsp_ir_init(&g_ir_events);
    bool level = bsp_ir_level();
    ef_init(&g_ir_filter, level, g_cfg.debounce_us);
    button_init(&g_button, BTN_DEBOUNCE_MS, BTN_LONG_MS, BTN_DOUBLE_MS);

    const app_port_t port = { .emit = on_app_event, .ctx = NULL };
    app_init(&g_app, &g_cfg, &port, level_to_blocked(level), bsp_time_ms());

    g_cli_ctx.app   = &g_app;
    g_cli_ctx.cfg   = &g_cfg;
    g_cli_ctx.store = bsp_config_flash();
    g_cli_ctx.plat  = &k_platform;
    app_cli_init(&g_cli, &g_cli_ctx, out_write);

    sched_init(&g_sched, g_tasks, ARRAY_SIZE(g_tasks), bsp_time_us, bsp_time_ms());
    bsp_wdg_init(WATCHDOG_TIMEOUT_MS);

    LOG_I(TAG, "mode %s, beam %s. Type 'help' for commands.", app_mode_name(g_app.mode),
          g_app.beam_blocked ? "blocked" : "clear");
    cli_prompt(&g_cli);
}

void fw_poll(void)
{
    uint8_t c;
    while (bsp_uart_getc(&c)) {
        cli_feed(&g_cli, (char)c);
    }

    process_ir();
    (void)sched_run(&g_sched, bsp_time_ms());

    /* Sleep until the next interrupt unless work is already waiting. */
    if (eq_count(&g_ir_events) == 0u && !g_ir_filter.pending) {
        __WFI();
    }
}
