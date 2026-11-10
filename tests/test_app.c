/**
 * @file    test_app.c
 * @brief   Tests for the CLI engine, the application state machine and the
 *          application commands, driven entirely from the host.
 */
#include "test.h"

#include "app/app.h"
#include "app/app_cli.h"
#include "app/cli.h"
#include "app/config.h"
#include "app/log.h"

/* ---- captured output ------------------------------------------------------ */

static char   s_out[8192];
static size_t s_out_len;

static void out_reset(void)
{
    s_out_len = 0;
    s_out[0]  = '\0';
}

static void out_write(const char *s, size_t len)
{
    if (s_out_len + len >= sizeof(s_out)) {
        len = sizeof(s_out) - 1 - s_out_len;
    }
    memcpy(&s_out[s_out_len], s, len);
    s_out_len += len;
    s_out[s_out_len] = '\0';
}

static bool out_has(const char *needle)
{
    return strstr(s_out, needle) != NULL;
}

/* ---- CLI engine ----------------------------------------------------------- */

static int  s_echo_argc;
static char s_echo_args[CLI_ARGV_MAX][32];

static int cmd_echo(cli_t *cli, int argc, char **argv)
{
    (void)cli;
    s_echo_argc = argc;
    for (int i = 0; i < argc; i++) {
        snprintf(s_echo_args[i], sizeof(s_echo_args[i]), "%s", argv[i]);
    }
    return CLI_OK;
}

static int cmd_fail(cli_t *cli, int argc, char **argv)
{
    (void)cli;
    (void)argc;
    (void)argv;
    return CLI_ERR_ARGS;
}

static const cli_command_t k_test_cmds[] = {
    { "echo", "[args...]", "print arguments", cmd_echo },
    { "fail", "<x>", "always fails", cmd_fail },
};

static void feed(cli_t *cli, const char *s)
{
    while (*s != '\0') {
        cli_feed(cli, *s++);
    }
}

static void test_cli_tokenize_quotes(void)
{
    char  line[] = "  set  name \"two words\"  3 ";
    char *argv[CLI_ARGV_MAX];
    CHECK_EQ(cli_tokenize(line, argv, CLI_ARGV_MAX), 4);
    CHECK_STR(argv[0], "set");
    CHECK_STR(argv[2], "two words");
    CHECK_STR(argv[3], "3");

    char bad[] = "echo \"unterminated";
    CHECK_EQ(cli_tokenize(bad, argv, CLI_ARGV_MAX), CLI_ERR_SYNTAX);
    char many[] = "a b c";
    CHECK_EQ(cli_tokenize(many, argv, 2), CLI_ERR_SYNTAX);
}

static void test_cli_parse_numbers(void)
{
    uint32_t v;
    CHECK(cli_parse_u32("1234", &v) && v == 1234u);
    CHECK(cli_parse_u32("0x1F", &v) && v == 31u);
    CHECK(cli_parse_u32("1_000_000", &v) && v == 1000000u);
    CHECK(cli_parse_u32("4294967295", &v) && v == UINT32_MAX);
    CHECK(!cli_parse_u32("4294967296", &v));
    CHECK(!cli_parse_u32("12a", &v));
    CHECK(!cli_parse_u32("", &v));
    CHECK(!cli_parse_u32("0x", &v));
    bool b;
    CHECK(cli_parse_bool("ON", &b) && b);
    CHECK(cli_parse_bool("0", &b) && !b);
    CHECK(!cli_parse_bool("maybe", &b));
}

static void test_cli_line_editing(void)
{
    cli_t cli;
    cli_init(&cli, k_test_cmds, 2, out_write, "> ", NULL);
    out_reset();
    s_echo_argc = 0;
    feed(&cli, "ecxo\b\bho hi\r\n"); /* backspace twice, CRLF counts once */
    CHECK_EQ(s_echo_argc, 2);
    CHECK_STR(s_echo_args[1], "hi");
    CHECK_EQ(cli.last_result, CLI_OK);

    s_echo_argc = 0;
    feed(&cli, "\x1b[A\r"); /* up arrow recalls "echo hi" */
    CHECK_EQ(s_echo_argc, 2);

    s_echo_argc = 0;
    feed(&cli, "echo nope\x03"); /* Ctrl-C discards the line */
    CHECK_EQ(s_echo_argc, 0);
    CHECK(out_has("^C"));
}

static void test_cli_errors_and_help(void)
{
    cli_t cli;
    cli_init(&cli, k_test_cmds, 2, out_write, "> ", NULL);
    out_reset();
    CHECK_EQ(cli_execute(&cli, "bogus"), CLI_ERR_UNKNOWN);
    CHECK(out_has("unknown command"));
    out_reset();
    CHECK_EQ(cli_execute(&cli, "FAIL"), CLI_ERR_ARGS); /* case-insensitive */
    CHECK(out_has("usage: fail <x>"));
    out_reset();
    CHECK_EQ(cli_execute(&cli, "help"), CLI_OK);
    CHECK(out_has("echo") && out_has("always fails"));
    CHECK_EQ(cli_execute(&cli, "   "), CLI_OK);
}

static void test_cli_line_length_limit(void)
{
    cli_t cli;
    cli_init(&cli, k_test_cmds, 2, out_write, "> ", NULL);
    out_reset();
    for (int i = 0; i < 200; i++) {
        cli_feed(&cli, 'x');
    }
    CHECK_EQ(cli.len, CLI_LINE_MAX - 1);
    CHECK(out_has("\a"));
}

void suite_cli(void)
{
    RUN_TEST(test_cli_tokenize_quotes);
    RUN_TEST(test_cli_parse_numbers);
    RUN_TEST(test_cli_line_editing);
    RUN_TEST(test_cli_errors_and_help);
    RUN_TEST(test_cli_line_length_limit);
}

/* ---- application ---------------------------------------------------------- */

#define MAX_EVENTS 64
static app_event_t s_events[MAX_EVENTS];
static int         s_nevents;

static void capture_event(void *ctx, const app_event_t *e)
{
    (void)ctx;
    if (s_nevents < MAX_EVENTS) {
        s_events[s_nevents++] = *e;
    }
}

static int count_events(app_event_type_t type)
{
    int n = 0;
    for (int i = 0; i < s_nevents; i++) {
        n += (s_events[i].type == type);
    }
    return n;
}

static const app_event_t *last_event(app_event_type_t type)
{
    for (int i = s_nevents - 1; i >= 0; i--) {
        if (s_events[i].type == type) {
            return &s_events[i];
        }
    }
    return NULL;
}

static config_t s_cfg;
static app_t    s_app;

static void app_setup(void)
{
    static const app_port_t port = { capture_event, NULL };
    log_init(NULL, NULL);
    config_set_defaults(&s_cfg);
    s_nevents = 0;
    app_init(&s_app, &s_cfg, &port, false, 0);
}

/* One object: blocked at t_us for width_us. */
static void pass_object(uint32_t t_us, uint32_t width_us, uint32_t now_ms)
{
    app_on_beam(&s_app, true, t_us, now_ms);
    app_on_beam(&s_app, false, t_us + width_us, now_ms);
}

static void test_app_counts_and_measures(void)
{
    app_setup();
    pass_object(1000, 4000, 1);
    pass_object(21000, 6000, 21);
    CHECK_EQ(s_app.stats.total, 2);
    const app_event_t *e = last_event(APP_EVT_OBJECT);
    CHECK(e != NULL);
    CHECK_EQ(e->a, 2);     /* seq   */
    CHECK_EQ(e->b, 6000);  /* width */
    CHECK_EQ(e->c, 20000); /* gap   */
    CHECK_EQ(count_events(APP_EVT_BEAM), 4);
}

static void test_app_batches_in_counter_mode(void)
{
    app_setup();
    s_cfg.batch_target = 3;
    for (uint32_t i = 0; i < 7; i++) {
        pass_object(i * 10000u, 1000, i * 10u);
    }
    CHECK_EQ(s_app.batches_done, 2);
    CHECK_EQ(s_app.batch_count, 1);
    CHECK_EQ(count_events(APP_EVT_BATCH), 2);
    app_tick(&s_app, 65);
    CHECK_EQ(s_app.led.pattern, LED_ON); /* batch celebration */
    app_tick(&s_app, 65 + APP_BATCH_SHOW_MS);
    CHECK_EQ(s_app.led.pattern, LED_HEARTBEAT);

    app_set_mode(&s_app, MODE_MONITOR, 2000);
    pass_object(900000, 1000, 2000);
    CHECK_EQ(s_app.batch_count, 1); /* batches only count in counter mode */
}

static void test_app_rejects_short_objects(void)
{
    app_setup();
    s_cfg.min_width_us = 2000;
    pass_object(0, 500, 1);
    pass_object(10000, 2500, 2);
    CHECK_EQ(s_app.stats.total, 1);
    CHECK_EQ(s_app.stats.rejected, 1);
    CHECK_EQ(count_events(APP_EVT_REJECT), 1);
}

static void test_app_unknown_block_start_not_counted(void)
{
    static const app_port_t port = { capture_event, NULL };
    config_set_defaults(&s_cfg);
    s_nevents = 0;
    app_init(&s_app, &s_cfg, &port, true, 0); /* beam already blocked at boot */
    app_on_beam(&s_app, false, 5000, 5);
    CHECK_EQ(s_app.stats.total, 0);
}

static void test_app_jam_alarm_and_ack(void)
{
    app_setup();
    app_on_beam(&s_app, true, 0, 100);
    app_tick(&s_app, 100 + s_cfg.jam_timeout_ms - 1);
    CHECK_EQ(s_app.alarms, 0);
    app_tick(&s_app, 100 + s_cfg.jam_timeout_ms);
    CHECK_EQ(s_app.alarms, ALARM_JAM);
    CHECK_EQ(s_app.led.pattern, LED_FAST_BLINK);
    CHECK_EQ(app_ack(&s_app, 4000), 0); /* still active: can't clear */

    app_on_beam(&s_app, false, 4000000, 4000);
    CHECK_EQ(s_app.alarms, 0);
    CHECK_EQ(s_app.latched, ALARM_JAM);
    app_on_button(&s_app, BTN_LONG, 4100); /* long press = ack */
    CHECK_EQ(s_app.latched, 0);
    CHECK_EQ(count_events(APP_EVT_ALARM_SET), 1);
    CHECK_EQ(count_events(APP_EVT_ALARM_CLEAR), 1);
}

static void test_app_rate_alarm_hysteresis(void)
{
    app_setup();
    s_cfg.rate_alarm_per_min = 10;
    for (uint32_t i = 0; i < 11; i++) {
        pass_object(i * 100000u, 1000, 1000u + i * 100u);
    }
    app_tick(&s_app, 2200);
    CHECK(s_app.alarms & ALARM_RATE);
    /* 61 s later the window is empty -> below 90 % -> inactive, still latched. */
    app_tick(&s_app, 63000);
    CHECK(!(s_app.alarms & ALARM_RATE));
    CHECK(s_app.latched & ALARM_RATE);
}

static void test_app_guard_arming_and_intrusion(void)
{
    app_setup();
    app_on_button(&s_app, BTN_SHORT, 0); /* counter -> monitor */
    app_on_button(&s_app, BTN_SHORT, 0); /* monitor -> guard   */
    CHECK_EQ(s_app.mode, MODE_GUARD);
    CHECK_EQ(s_app.guard, GUARD_ARMING);

    pass_object(1000, 100, 1000); /* during exit delay: no alarm */
    CHECK_EQ(s_app.latched, 0);

    app_tick(&s_app, APP_ARM_DELAY_MS);
    CHECK_EQ(s_app.guard, GUARD_ARMED);
    app_on_beam(&s_app, true, 9000000, 6000);
    CHECK(s_app.latched & ALARM_INTRUSION);
    CHECK(!(s_app.alarms & ALARM_INTRUSION)); /* momentary */

    app_on_beam(&s_app, false, 9001000, 6001);
    app_on_button(&s_app, BTN_LONG, 7000); /* ack */
    CHECK_EQ(s_app.latched, 0);
    app_on_button(&s_app, BTN_LONG, 8000); /* no alarm now: disarm */
    CHECK_EQ(s_app.guard, GUARD_DISARMED);
    app_on_button(&s_app, BTN_SHORT, 9000); /* guard -> counter */
    CHECK_EQ(s_app.mode, MODE_COUNTER);
}

static void test_app_pause_and_reset(void)
{
    app_setup();
    app_on_button(&s_app, BTN_LONG, 0); /* no alarm, counter mode: pause */
    CHECK(s_app.paused);
    pass_object(0, 1000, 1);
    CHECK_EQ(s_app.stats.total, 0);
    app_set_paused(&s_app, false, 2);
    pass_object(10000, 1000, 3);
    CHECK_EQ(s_app.stats.total, 1);
    app_on_button(&s_app, BTN_DOUBLE, 4);
    CHECK_EQ(s_app.stats.total, 0);
    CHECK_EQ(count_events(APP_EVT_RESET), 1);
}

static void test_app_simulate(void)
{
    app_setup();
    CHECK(app_simulate(&s_app, 5, 2000, 8000, 1000000, 50));
    CHECK_EQ(s_app.stats.total, 5);
    CHECK_EQ(s_app.stats.width_us.min, 2000);
    CHECK_EQ(summary_avg(&s_app.stats.gap_us), 10000);
    app_on_beam(&s_app, true, 2000000, 60);
    CHECK(!app_simulate(&s_app, 1, 1, 1, 2000001, 61)); /* beam blocked */
}

static void test_app_alarm_names(void)
{
    char buf[48];
    CHECK_STR(app_alarm_names(0, buf, sizeof(buf)), "none");
    CHECK_STR(app_alarm_names(ALARM_JAM | ALARM_OVERFLOW, buf, sizeof(buf)), "JAM|OVERFLOW");
    app_mode_t m;
    CHECK(app_mode_parse("Guard", &m) && m == MODE_GUARD);
    CHECK(!app_mode_parse("sleep", &m));
}

static void test_app_boot_mode(void)
{
    static const app_port_t port = { capture_event, NULL };
    config_set_defaults(&s_cfg);
    s_cfg.boot_mode = MODE_GUARD;
    app_init(&s_app, &s_cfg, &port, false, 0);
    CHECK_EQ(s_app.mode, MODE_GUARD);
    CHECK_EQ(s_app.guard, GUARD_ARMING);
}

void suite_app(void)
{
    RUN_TEST(test_app_counts_and_measures);
    RUN_TEST(test_app_batches_in_counter_mode);
    RUN_TEST(test_app_rejects_short_objects);
    RUN_TEST(test_app_unknown_block_start_not_counted);
    RUN_TEST(test_app_jam_alarm_and_ack);
    RUN_TEST(test_app_rate_alarm_hysteresis);
    RUN_TEST(test_app_guard_arming_and_intrusion);
    RUN_TEST(test_app_pause_and_reset);
    RUN_TEST(test_app_simulate);
    RUN_TEST(test_app_alarm_names);
    RUN_TEST(test_app_boot_mode);
}

/* ---- application commands ------------------------------------------------- */

static uint32_t s_now_ms = 1000;
static uint32_t s_now_us = 50000000;
static int      s_changed;

static uint32_t plat_ms(void) { return s_now_ms; }
static uint32_t plat_us(void) { return s_now_us; }
static void     plat_changed(void) { s_changed++; }

/* RAM-backed flash for save/load. */
static uint8_t s_store_mem[512];
static bool st_read(void *c, uint32_t o, void *d, uint32_t n) { (void)c; memcpy(d, &s_store_mem[o], n); return o + n <= sizeof(s_store_mem); }
static bool st_prog(void *c, uint32_t o, const void *s, uint32_t n)
{
    (void)c;
    const uint8_t *p = s;
    for (uint32_t i = 0; i < n; i++) {
        s_store_mem[o + i] &= p[i];
    }
    return true;
}
static bool st_erase(void *c) { (void)c; memset(s_store_mem, 0xFF, sizeof(s_store_mem)); return true; }

static void test_app_cli_commands(void)
{
    static const app_platform_t plat = { plat_ms, plat_us, NULL, NULL, NULL, plat_changed };
    static const flash_region_t store = { sizeof(s_store_mem), st_read, st_prog, st_erase, NULL };
    static app_cli_ctx_t        ctx;
    cli_t                       cli;

    memset(s_store_mem, 0xFF, sizeof(s_store_mem));
    app_setup();
    ctx.app   = &s_app;
    ctx.cfg   = &s_cfg;
    ctx.store = &store;
    ctx.plat  = &plat;
    app_cli_init(&cli, &ctx, out_write);

    out_reset();
    CHECK_EQ(cli_execute(&cli, "sim 4 3000 7000"), CLI_OK);
    CHECK_EQ(cli_execute(&cli, "status"), CLI_OK);
    CHECK(out_has("objects   : 4"));
    CHECK(out_has("min 3000  avg 3000  max 3000"));

    out_reset();
    CHECK_EQ(cli_execute(&cli, "set batch 25"), CLI_OK);
    CHECK_EQ(s_cfg.batch_target, 25);
    CHECK_EQ(s_changed, 1);
    CHECK_EQ(cli_execute(&cli, "set debounce_us 1"), CLI_ERR_RANGE);
    CHECK(out_has("debounce_us must be 50..100000"));
    CHECK_EQ(cli_execute(&cli, "set stream on"), CLI_OK);
    CHECK_EQ(s_cfg.stream, 1);
    CHECK_EQ(cli_execute(&cli, "set nothing 1"), CLI_ERR_ARGS);

    out_reset();
    CHECK_EQ(cli_execute(&cli, "save"), CLI_OK);
    CHECK(out_has("saved (record #1"));
    CHECK_EQ(cli_execute(&cli, "defaults"), CLI_OK);
    CHECK_EQ(s_cfg.batch_target, 10);
    CHECK_EQ(cli_execute(&cli, "load"), CLI_OK);
    CHECK_EQ(s_cfg.batch_target, 25);

    CHECK_EQ(cli_execute(&cli, "mode guard"), CLI_OK);
    CHECK_EQ(s_app.mode, MODE_GUARD);
    CHECK_EQ(cli_execute(&cli, "disarm"), CLI_OK);
    CHECK_EQ(s_app.guard, GUARD_DISARMED);
    CHECK_EQ(cli_execute(&cli, "mode nonsense"), CLI_ERR_ARGS);
    CHECK_EQ(cli_execute(&cli, "mode counter"), CLI_OK);
    CHECK_EQ(cli_execute(&cli, "arm"), CLI_ERR_FAIL);

    CHECK_EQ(cli_execute(&cli, "log debug"), CLI_OK);
    CHECK_EQ(log_get_level(), LOG_LEVEL_DEBUG);
    CHECK_EQ(cli_execute(&cli, "log info"), CLI_OK);

    out_reset();
    CHECK_EQ(cli_execute(&cli, "config"), CLI_OK);
    CHECK(out_has("batch") && out_has("25*"));
    CHECK_EQ(cli_execute(&cli, "reset"), CLI_OK);
    CHECK_EQ(s_app.stats.total, 0);
    CHECK_EQ(cli_execute(&cli, "sys"), CLI_ERR_FAIL); /* no platform hook */
    CHECK_EQ(cli_execute(&cli, "sim 0"), CLI_ERR_RANGE);
    CHECK_EQ(cli_execute(&cli, "version"), CLI_OK);
}

void suite_app_cli(void)
{
    RUN_TEST(test_app_cli_commands);
}
