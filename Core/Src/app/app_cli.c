/**
 * @file    app_cli.c
 * @brief   IRSense serial commands.
 */
#include "app/app_cli.h"
#include "app/log.h"
#include "app/util.h"
#include "app/version.h"

#include <stdio.h>
#include <string.h>

#define CTX(cli) ((app_cli_ctx_t *)(cli)->ctx)

static uint32_t now_ms(cli_t *cli)
{
    const app_platform_t *p = CTX(cli)->plat;
    return (p != NULL && p->now_ms != NULL) ? p->now_ms() : 0u;
}

static uint32_t now_us(cli_t *cli)
{
    const app_platform_t *p = CTX(cli)->plat;
    return (p != NULL && p->now_us != NULL) ? p->now_us() : 0u;
}

/* Applies settings that need more than a field change. */
static void config_applied(cli_t *cli)
{
    app_cli_ctx_t *c = CTX(cli);
    log_set_level((log_level_t)c->cfg->log_level);
    if (c->plat != NULL && c->plat->config_changed != NULL) {
        c->plat->config_changed();
    }
}

static void print_summary(cli_t *cli, const char *label, const summary_t *s)
{
    if (s->n == 0u) {
        cli_printf(cli, "%-10s: -\r\n", label);
        return;
    }
    cli_printf(cli, "%-10s: min %lu  avg %lu  max %lu  (n=%lu)\r\n", label,
               (unsigned long)s->min, (unsigned long)summary_avg(s), (unsigned long)s->max,
               (unsigned long)s->n);
}

/* ------------------------------------------------------------------------ */

static int cmd_version(cli_t *cli, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    /* No __DATE__/__TIME__: builds are reproducible; the git hash identifies them. */
    cli_printf(cli, "%s %s (git %s) for %s\r\n", FW_NAME, FW_VERSION, FW_GIT_HASH, FW_BOARD);
    return CLI_OK;
}

static int cmd_status(cli_t *cli, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    app_cli_ctx_t *c   = CTX(cli);
    const app_t   *app = c->app;
    char           a[48];
    char           l[48];
    uint32_t       ms  = now_ms(cli);

    cli_printf(cli, "mode      : %s%s", app_mode_name(app->mode), app->paused ? " (paused)" : "");
    if (app->mode == MODE_GUARD) {
        cli_printf(cli, " [%s]", app_guard_name(app->guard));
    }
    cli_print(cli, "\r\n");
    cli_printf(cli, "beam      : %s\r\n", app->beam_blocked ? "BLOCKED" : "clear");
    cli_printf(cli, "objects   : %lu  (rejected %lu)\r\n", (unsigned long)app->stats.total,
               (unsigned long)app->stats.rejected);
    if (c->cfg->batch_target > 0u) {
        cli_printf(cli, "batch     : %lu/%lu  (%lu done)\r\n", (unsigned long)app->batch_count,
                   (unsigned long)c->cfg->batch_target, (unsigned long)app->batches_done);
    }
    cli_printf(cli, "rate      : %lu /min  (peak %lu)\r\n",
               (unsigned long)stats_rate_per_min(&app->stats), (unsigned long)app->stats.peak_rate);
    print_summary(cli, "width us", &app->stats.width_us);
    print_summary(cli, "gap us", &app->stats.gap_us);
    cli_printf(cli, "alarms    : %s  (latched %s)\r\n", app_alarm_names(app->alarms, a, sizeof(a)),
               app_alarm_names(app->latched, l, sizeof(l)));
    cli_printf(cli, "uptime    : %lu.%03lu s\r\n", (unsigned long)(ms / 1000u),
               (unsigned long)(ms % 1000u));
    return CLI_OK;
}

static int cmd_reset(cli_t *cli, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    app_reset_counters(CTX(cli)->app, now_ms(cli));
    cli_print(cli, "counters reset\r\n");
    return CLI_OK;
}

static int cmd_mode(cli_t *cli, int argc, char **argv)
{
    app_t *app = CTX(cli)->app;
    if (argc == 1) {
        cli_printf(cli, "%s\r\n", app_mode_name(app->mode));
        return CLI_OK;
    }
    app_mode_t m;
    if (argc != 2 || !app_mode_parse(argv[1], &m)) {
        return CLI_ERR_ARGS;
    }
    app_set_mode(app, m, now_ms(cli));
    cli_printf(cli, "mode %s\r\n", app_mode_name(app->mode));
    return CLI_OK;
}

static int cmd_pause(cli_t *cli, int argc, char **argv)
{
    (void)argc;
    bool pause = str_ieq(argv[0], "pause");
    app_set_paused(CTX(cli)->app, pause, now_ms(cli));
    cli_printf(cli, "%s\r\n", pause ? "paused" : "running");
    return CLI_OK;
}

static int cmd_arm(cli_t *cli, int argc, char **argv)
{
    (void)argc;
    app_t *app = CTX(cli)->app;
    if (app->mode != MODE_GUARD) {
        cli_print(cli, "only in guard mode (mode guard)\r\n");
        return CLI_ERR_FAIL;
    }
    app_set_armed(app, str_ieq(argv[0], "arm"), now_ms(cli));
    cli_printf(cli, "guard %s\r\n", app_guard_name(app->guard));
    return CLI_OK;
}

static int cmd_ack(cli_t *cli, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    char     buf[48];
    app_t   *app     = CTX(cli)->app;
    uint32_t cleared = app_ack(app, now_ms(cli));
    cli_printf(cli, "cleared %s", app_alarm_names(cleared, buf, sizeof(buf)));
    if (app->alarms != 0u) {
        cli_printf(cli, ", still active %s", app_alarm_names(app->alarms, buf, sizeof(buf)));
    }
    cli_print(cli, "\r\n");
    return CLI_OK;
}

static int cmd_config(cli_t *cli, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    const config_t *cfg = CTX(cli)->cfg;
    cli_printf(cli, "%-13s %10s  %-5s %-17s %s\r\n", "name", "value", "unit", "range", "description");
    for (size_t i = 0u; i < config_param_count(); i++) {
        const config_param_t *p = config_param_at(i);
        char range[24];
        snprintf(range, sizeof(range), "%lu..%lu", (unsigned long)p->min, (unsigned long)p->max);
        cli_printf(cli, "%-13s %10lu%s %-5s %-17s %s\r\n", p->name,
                   (unsigned long)config_get(cfg, p), (config_get(cfg, p) != p->def) ? "*" : " ",
                   p->unit, range, p->help);
    }
    cli_print(cli, "(* = changed from default; 'save' makes changes permanent)\r\n");
    return CLI_OK;
}

static int cmd_get(cli_t *cli, int argc, char **argv)
{
    if (argc != 2) {
        return CLI_ERR_ARGS;
    }
    const config_param_t *p = config_param_find(argv[1]);
    if (p == NULL) {
        cli_printf(cli, "no setting '%s' (see 'config')\r\n", argv[1]);
        return CLI_ERR_ARGS;
    }
    cli_printf(cli, "%s = %lu\r\n", p->name, (unsigned long)config_get(CTX(cli)->cfg, p));
    return CLI_OK;
}

static int cmd_set(cli_t *cli, int argc, char **argv)
{
    uint32_t v;
    if (argc != 3) {
        return CLI_ERR_ARGS;
    }
    if (!cli_parse_u32(argv[2], &v)) {
        bool b;
        if (!cli_parse_bool(argv[2], &b)) {
            return CLI_ERR_ARGS;
        }
        v = b ? 1u : 0u;
    }
    switch (config_set(CTX(cli)->cfg, argv[1], v)) {
    case CONFIG_OK:
        break;
    case CONFIG_ERR_UNKNOWN:
        cli_printf(cli, "no setting '%s' (see 'config')\r\n", argv[1]);
        return CLI_ERR_ARGS;
    case CONFIG_ERR_RANGE:
    default: {
        const config_param_t *p = config_param_find(argv[1]);
        cli_printf(cli, "%s must be %lu..%lu\r\n", p->name, (unsigned long)p->min,
                   (unsigned long)p->max);
        return CLI_ERR_RANGE;
    }
    }
    config_applied(cli);
    const config_param_t *p = config_param_find(argv[1]);
    cli_printf(cli, "%s = %lu\r\n", p->name, (unsigned long)config_get(CTX(cli)->cfg, p));
    return CLI_OK;
}

static int cmd_save(cli_t *cli, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    app_cli_ctx_t *c = CTX(cli);
    if (c->store == NULL) {
        return CLI_ERR_FAIL;
    }
    config_store_info_t info;
    cfg_store_status_t  st = config_store_save(c->store, c->cfg, &info);
    if (st != CFG_STORE_OK) {
        cli_printf(cli, "save failed: %s\r\n", config_store_status_name(st));
        return CLI_ERR_FAIL;
    }
    cli_printf(cli, "saved (record #%lu, slot %lu/%lu%s)\r\n", (unsigned long)info.last_seq,
               (unsigned long)info.slots_used, (unsigned long)info.slot_count,
               (info.erases > 0u) ? ", sector erased" : "");
    return CLI_OK;
}

static int cmd_load(cli_t *cli, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    app_cli_ctx_t *c = CTX(cli);
    if (c->store == NULL) {
        return CLI_ERR_FAIL;
    }
    config_t            tmp;
    config_store_info_t info;
    cfg_store_status_t  st = config_store_load(c->store, &tmp, &info);
    if (st != CFG_STORE_OK) {
        cli_printf(cli, "load: %s\r\n", config_store_status_name(st));
        return (st == CFG_STORE_EMPTY) ? CLI_OK : CLI_ERR_FAIL;
    }
    *c->cfg = tmp;
    config_applied(cli);
    cli_printf(cli, "loaded record #%lu\r\n", (unsigned long)info.last_seq);
    return CLI_OK;
}

static int cmd_defaults(cli_t *cli, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    config_set_defaults(CTX(cli)->cfg);
    config_applied(cli);
    cli_print(cli, "defaults restored (not saved; run 'save' to keep them)\r\n");
    return CLI_OK;
}

static int cmd_stream(cli_t *cli, int argc, char **argv)
{
    app_cli_ctx_t *c = CTX(cli);
    if (argc == 2) {
        bool on;
        if (!cli_parse_bool(argv[1], &on)) {
            return CLI_ERR_ARGS;
        }
        c->cfg->stream = on ? 1u : 0u;
        config_applied(cli);
    } else if (argc != 1) {
        return CLI_ERR_ARGS;
    }
    cli_printf(cli, "stream %s\r\n", c->cfg->stream ? "on" : "off");
    return CLI_OK;
}

static int cmd_log(cli_t *cli, int argc, char **argv)
{
    app_cli_ctx_t *c = CTX(cli);
    if (argc == 2) {
        log_level_t lvl;
        if (!log_level_parse(argv[1], &lvl)) {
            return CLI_ERR_ARGS;
        }
        c->cfg->log_level = (uint32_t)lvl;
        config_applied(cli);
    } else if (argc != 1) {
        return CLI_ERR_ARGS;
    }
    cli_printf(cli, "log level %s\r\n", log_level_name(log_get_level()));
    return CLI_OK;
}

static int cmd_sim(cli_t *cli, int argc, char **argv)
{
    uint32_t count, width = 5000u, gap = 20000u;
    if (argc < 2 || argc > 4 || !cli_parse_u32(argv[1], &count)) {
        return CLI_ERR_ARGS;
    }
    if ((argc >= 3 && !cli_parse_u32(argv[2], &width)) ||
        (argc >= 4 && !cli_parse_u32(argv[3], &gap))) {
        return CLI_ERR_ARGS;
    }
    if (count == 0u || count > 10000u || width == 0u || width > 1000000u || gap > 1000000u) {
        cli_print(cli, "count 1..10000, width 1..1000000 us, gap 0..1000000 us\r\n");
        return CLI_ERR_RANGE;
    }
    if (!app_simulate(CTX(cli)->app, count, width, gap, now_us(cli), now_ms(cli))) {
        cli_print(cli, "beam is blocked; clear it first\r\n");
        return CLI_ERR_FAIL;
    }
    cli_printf(cli, "simulated %lu objects (width %lu us, gap %lu us)\r\n", (unsigned long)count,
               (unsigned long)width, (unsigned long)gap);
    return CLI_OK;
}

static int cmd_sys(cli_t *cli, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    const app_platform_t *p = CTX(cli)->plat;
    if (p == NULL || p->print_sys == NULL) {
        return CLI_ERR_FAIL;
    }
    p->print_sys(cli);
    return CLI_OK;
}

static int cmd_tasks(cli_t *cli, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    const app_platform_t *p = CTX(cli)->plat;
    if (p == NULL || p->print_tasks == NULL) {
        return CLI_ERR_FAIL;
    }
    p->print_tasks(cli);
    return CLI_OK;
}

static int cmd_reboot(cli_t *cli, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    const app_platform_t *p = CTX(cli)->plat;
    if (p == NULL || p->reboot == NULL) {
        return CLI_ERR_FAIL;
    }
    cli_print(cli, "rebooting...\r\n");
    p->reboot();
    return CLI_OK;
}

static const cli_command_t k_commands[] = {
    { "status",   "",                       "counters, rates, timing and alarms",   cmd_status },
    { "version",  "",                       "firmware version and build",           cmd_version },
    { "reset",    "",                       "reset all counters and statistics",    cmd_reset },
    { "mode",     "[counter|monitor|guard]", "show or change the operating mode",   cmd_mode },
    { "pause",    "",                       "stop counting",                        cmd_pause },
    { "resume",   "",                       "resume counting",                      cmd_pause },
    { "arm",      "",                       "arm guard mode (after 5 s delay)",     cmd_arm },
    { "disarm",   "",                       "disarm guard mode",                    cmd_arm },
    { "ack",      "",                       "acknowledge latched alarms",           cmd_ack },
    { "config",   "",                       "list all settings",                    cmd_config },
    { "get",      "<name>",                 "show one setting",                     cmd_get },
    { "set",      "<name> <value>",         "change a setting (live)",              cmd_set },
    { "save",     "",                       "store settings in flash",              cmd_save },
    { "load",     "",                       "reload settings from flash",           cmd_load },
    { "defaults", "",                       "restore factory settings",             cmd_defaults },
    { "stream",   "[on|off]",               "machine-readable $-records",           cmd_stream },
    { "log",      "[error|warn|info|debug]", "show or set the log level",           cmd_log },
    { "sim",      "<n> [width_us] [gap_us]", "inject n simulated objects",          cmd_sim },
    { "tasks",    "",                       "scheduler task timing",                cmd_tasks },
    { "sys",      "",                       "system info (clock, reset, uid, ...)", cmd_sys },
    { "reboot",   "",                       "software reset",                       cmd_reboot },
};

void app_cli_init(cli_t *cli, app_cli_ctx_t *ctx, cli_write_t write)
{
    cli_init(cli, k_commands, ARRAY_SIZE(k_commands), write, "irsense> ", ctx);
}
