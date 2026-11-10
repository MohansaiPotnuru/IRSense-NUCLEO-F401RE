/**
 * @file    test_core.c
 * @brief   Tests for the small building blocks: ring buffer, CRC, event
 *          queue, edge filter, button, stats, LED patterns, scheduler, log.
 */
#include "test.h"

#include "app/button.h"
#include "app/crc32.h"
#include "app/edge_filter.h"
#include "app/event_queue.h"
#include "app/led_pattern.h"
#include "app/log.h"
#include "app/ring_buffer.h"
#include "app/scheduler.h"
#include "app/stats.h"

/* ---- ring buffer --------------------------------------------------------- */

static void test_rb_init_rejects_bad_capacity(void)
{
    ring_buffer_t rb;
    uint8_t       buf[16];
    CHECK(!rb_init(&rb, buf, 0));
    CHECK(!rb_init(&rb, buf, 12));
    CHECK(!rb_init(&rb, NULL, 16));
    CHECK(rb_init(&rb, buf, 16));
    CHECK_EQ(rb_capacity(&rb), 16);
    CHECK(rb_is_empty(&rb));
}

static void test_rb_fifo_order_and_full(void)
{
    ring_buffer_t rb;
    uint8_t       buf[4];
    uint8_t       v = 0;
    rb_init(&rb, buf, 4);
    for (uint8_t i = 0; i < 4; i++) {
        CHECK(rb_put(&rb, (uint8_t)(i + 10)));
    }
    CHECK(rb_is_full(&rb));
    CHECK(!rb_put(&rb, 99)); /* all 4 slots usable, 5th refused */
    CHECK(rb_peek(&rb, &v));
    CHECK_EQ(v, 10);
    for (uint8_t i = 0; i < 4; i++) {
        CHECK(rb_get(&rb, &v));
        CHECK_EQ(v, i + 10);
    }
    CHECK(!rb_get(&rb, &v));
}

static void test_rb_wraps_counters(void)
{
    ring_buffer_t rb;
    uint8_t       buf[8];
    uint8_t       out[8];
    rb_init(&rb, buf, 8);
    rb.head = rb.tail = 0xFFFFFFFCu; /* force counter wrap-around */
    CHECK_EQ(rb_write(&rb, "abcdefghij", 10), 8);
    CHECK_EQ(rb_count(&rb), 8);
    CHECK_EQ(rb_read(&rb, out, 8), 8);
    CHECK(memcmp(out, "abcdefgh", 8) == 0);
    CHECK(rb_is_empty(&rb));
    rb_put(&rb, 1);
    rb_clear(&rb);
    CHECK(rb_is_empty(&rb));
}

void suite_ring_buffer(void)
{
    RUN_TEST(test_rb_init_rejects_bad_capacity);
    RUN_TEST(test_rb_fifo_order_and_full);
    RUN_TEST(test_rb_wraps_counters);
}

/* ---- crc32 ----------------------------------------------------------------- */

static void test_crc32_known_vectors(void)
{
    CHECK_EQ(crc32("123456789", 9), 0xCBF43926u);
    CHECK_EQ(crc32("", 0), 0u);
    CHECK_EQ(crc32("The quick brown fox jumps over the lazy dog", 43), 0x414FA339u);
}

static void test_crc32_chaining(void)
{
    uint32_t c = crc32_update(0, "1234", 4);
    c          = crc32_update(c, "56789", 5);
    CHECK_EQ(c, 0xCBF43926u);
}

void suite_crc32(void)
{
    RUN_TEST(test_crc32_known_vectors);
    RUN_TEST(test_crc32_chaining);
}

/* ---- event queue ----------------------------------------------------------- */

static void test_eq_overflow_counts_drops(void)
{
    static event_queue_t q;
    event_t              e = { 0, EVT_IR_EDGE, 0 };
    eq_init(&q);
    for (uint32_t i = 0; i < EVENT_QUEUE_CAPACITY; i++) {
        e.timestamp_us = i;
        CHECK(eq_push(&q, &e));
    }
    CHECK(!eq_push(&q, &e));
    CHECK(!eq_push(&q, &e));
    CHECK_EQ(q.dropped, 2);
    CHECK_EQ(q.high_water, EVENT_QUEUE_CAPACITY);
    CHECK_EQ(eq_count(&q), EVENT_QUEUE_CAPACITY);

    event_t out;
    for (uint32_t i = 0; i < EVENT_QUEUE_CAPACITY; i++) {
        CHECK(eq_pop(&q, &out));
        CHECK_EQ(out.timestamp_us, i);
    }
    CHECK(!eq_pop(&q, &out));
}

void suite_event_queue(void)
{
    RUN_TEST(test_eq_overflow_counts_drops);
}

/* ---- edge filter ----------------------------------------------------------- */

static void test_ef_accepts_stable_change_with_original_time(void)
{
    edge_filter_t f;
    bool          level;
    uint32_t      t;
    ef_init(&f, true, 1000);
    ef_raw_edge(&f, false, 5000);
    CHECK(!ef_poll(&f, 5999, &level, &t)); /* not stable long enough yet */
    CHECK(ef_poll(&f, 6000, &level, &t));
    CHECK(!level);
    CHECK_EQ(t, 5000); /* reported at the real edge, not at confirmation */
    CHECK(!ef_poll(&f, 9000, &level, &t));
}

static void test_ef_rejects_glitch(void)
{
    edge_filter_t f;
    bool          level;
    uint32_t      t;
    ef_init(&f, true, 1000);
    ef_raw_edge(&f, false, 100);
    ef_raw_edge(&f, true, 400); /* 300 us glitch */
    CHECK(!ef_poll(&f, 5000, &level, &t));
    CHECK_EQ(f.glitches, 1);
    CHECK(f.stable_level);
}

static void test_ef_ignores_duplicates_and_future_edges(void)
{
    edge_filter_t f;
    bool          level;
    uint32_t      t;
    ef_init(&f, false, 100);
    ef_raw_edge(&f, false, 10); /* same level: ignored */
    CHECK(!f.pending);
    ef_raw_edge(&f, true, 1000);
    CHECK(!ef_poll(&f, 900, &level, &t)); /* edge newer than "now" */
    CHECK(ef_poll(&f, 1100, &level, &t));
    CHECK(level);
}

static void test_ef_timer_wraparound(void)
{
    edge_filter_t f;
    bool          level;
    uint32_t      t;
    ef_init(&f, true, 2000);
    ef_raw_edge(&f, false, 0xFFFFFF00u);
    CHECK(!ef_poll(&f, 0x00000100u, &level, &t)); /* 512 us later */
    CHECK(ef_poll(&f, 0x00000800u, &level, &t));  /* 2304 us later */
    CHECK_EQ(t, 0xFFFFFF00u);
}

void suite_edge_filter(void)
{
    RUN_TEST(test_ef_accepts_stable_change_with_original_time);
    RUN_TEST(test_ef_rejects_glitch);
    RUN_TEST(test_ef_ignores_duplicates_and_future_edges);
    RUN_TEST(test_ef_timer_wraparound);
}

/* ---- button -------------------------------------------------------------- */

/* Drives the button with `pressed` from t0 to t1 (1 ms steps); returns the
 * first non-NONE event seen, or BTN_NONE. */
static btn_event_t drive(button_t *b, bool pressed, uint32_t t0, uint32_t t1)
{
    btn_event_t first = BTN_NONE;
    for (uint32_t t = t0; t < t1; t++) {
        btn_event_t e = button_update(b, pressed, t);
        if (e != BTN_NONE && first == BTN_NONE) {
            first = e;
        }
    }
    return first;
}

static void test_button_short_press(void)
{
    button_t b;
    button_init(&b, 20, 800, 300);
    CHECK_EQ(drive(&b, true, 0, 100), BTN_NONE);
    CHECK_EQ(drive(&b, false, 100, 200), BTN_NONE); /* waiting for a 2nd click */
    CHECK_EQ(drive(&b, false, 200, 600), BTN_SHORT);
}

static void test_button_double_press(void)
{
    button_t b;
    button_init(&b, 20, 800, 300);
    drive(&b, true, 0, 80);
    drive(&b, false, 80, 180);
    drive(&b, true, 180, 260);
    CHECK_EQ(drive(&b, false, 260, 400), BTN_DOUBLE);
    CHECK_EQ(drive(&b, false, 400, 1000), BTN_NONE); /* no stray SHORT */
}

static void test_button_long_press_fires_once(void)
{
    button_t b;
    button_init(&b, 20, 800, 300);
    CHECK_EQ(drive(&b, true, 0, 900), BTN_LONG);
    CHECK_EQ(drive(&b, true, 900, 3000), BTN_NONE);
    CHECK_EQ(drive(&b, false, 3000, 4000), BTN_NONE); /* release isn't a SHORT */
}

static void test_button_bounce_is_filtered(void)
{
    button_t b;
    button_init(&b, 20, 800, 300);
    for (uint32_t t = 0; t < 15; t++) {
        CHECK_EQ(button_update(&b, (t & 1u) != 0u, t), BTN_NONE); /* chatter */
    }
    CHECK(!b.pressed);
    CHECK_EQ(drive(&b, false, 15, 1000), BTN_NONE);
}

void suite_button(void)
{
    RUN_TEST(test_button_short_press);
    RUN_TEST(test_button_double_press);
    RUN_TEST(test_button_long_press_fires_once);
    RUN_TEST(test_button_bounce_is_filtered);
}

/* ---- stats ----------------------------------------------------------------- */

static void test_stats_width_and_gap(void)
{
    stats_t s;
    stats_init(&s, 0);
    CHECK_EQ(stats_add_object(&s, 1000, 300, 10), 0); /* first: no gap */
    CHECK_EQ(stats_add_object(&s, 6000, 500, 20), 5000);
    CHECK_EQ(stats_add_object(&s, 8000, 100, 30), 2000);
    CHECK_EQ(s.total, 3);
    CHECK_EQ(s.width_us.min, 100);
    CHECK_EQ(s.width_us.max, 500);
    CHECK_EQ(summary_avg(&s.width_us), 300);
    CHECK_EQ(s.gap_us.n, 2);
    CHECK_EQ(summary_avg(&s.gap_us), 3500);
}

static void test_stats_rate_window_slides(void)
{
    stats_t s;
    stats_init(&s, 0);
    for (uint32_t i = 0; i < 30; i++) {
        stats_add_object(&s, i * 100000u, 10, i * 1000u); /* 1 per second */
    }
    CHECK_EQ(stats_rate_per_min(&s), 30);
    stats_advance(&s, 59500);
    CHECK_EQ(stats_rate_per_min(&s), 30);
    stats_advance(&s, 75500); /* objects 0..15 have left the 60 s window */
    CHECK_EQ(stats_rate_per_min(&s), 14);
    stats_advance(&s, 500000); /* long idle */
    CHECK_EQ(stats_rate_per_min(&s), 0);
    CHECK_EQ(s.peak_rate, 30);
}

static void test_stats_summary_empty(void)
{
    summary_t s;
    summary_reset(&s);
    CHECK_EQ(summary_avg(&s), 0);
    summary_add(&s, 7);
    CHECK_EQ(s.min, 7);
    CHECK_EQ(s.max, 7);
}

void suite_stats(void)
{
    RUN_TEST(test_stats_width_and_gap);
    RUN_TEST(test_stats_rate_window_slides);
    RUN_TEST(test_stats_summary_empty);
}

/* ---- led pattern ----------------------------------------------------------- */

static void test_led_patterns(void)
{
    led_t led;
    led_init(&led, 0);
    CHECK(!led_level(&led, 123));
    led_set(&led, LED_SLOW_BLINK, 1000);
    CHECK(led_level(&led, 1000));
    CHECK(led_level(&led, 1499));
    CHECK(!led_level(&led, 1500));
    CHECK(led_level(&led, 2000)); /* period 1 s */
    led_set(&led, LED_SLOW_BLINK, 1700); /* same pattern: phase kept */
    CHECK(!led_level(&led, 1700));
    CHECK_STR(led_pattern_name(LED_HEARTBEAT), "heartbeat");
}

static void test_led_flash_inverts_then_expires(void)
{
    led_t led;
    led_init(&led, 0);
    led_set(&led, LED_ON, 0);
    led_flash(&led, 100, 40);
    CHECK(!led_level(&led, 120));
    CHECK(led_level(&led, 140));
    CHECK(!led.flash_active);
}

void suite_led_pattern(void)
{
    RUN_TEST(test_led_patterns);
    RUN_TEST(test_led_flash_inverts_then_expires);
}

/* ---- scheduler ------------------------------------------------------------- */

static int      s_calls_a;
static int      s_calls_b;
static uint32_t s_fake_us;

static void task_a(void *ctx, uint32_t now) { (void)ctx; (void)now; s_calls_a++; s_fake_us += 7; }
static void task_b(void *ctx, uint32_t now) { (void)ctx; (void)now; s_calls_b++; }
static uint32_t fake_clock_us(void) { return s_fake_us; }

static void test_scheduler_periods_and_overruns(void)
{
    task_t tasks[] = {
        { .name = "a", .fn = task_a, .period_ms = 10 },
        { .name = "b", .fn = task_b, .period_ms = 25 },
    };
    scheduler_t s;
    s_calls_a = s_calls_b = 0;
    sched_init(&s, tasks, 2, fake_clock_us, 0);
    for (uint32_t t = 0; t < 100; t++) {
        sched_run(&s, t);
    }
    CHECK_EQ(s_calls_a, 10); /* 0,10,...,90 */
    CHECK_EQ(s_calls_b, 4);  /* 0,25,50,75 */
    CHECK_EQ(tasks[0].max_us, 7);
    CHECK_EQ(tasks[0].overruns, 0);

    sched_run(&s, 1000); /* main loop stalled for ~900 ms */
    CHECK_EQ(tasks[0].overruns, 1);
    CHECK_EQ(tasks[0].next_ms, 1010);
}

void suite_scheduler(void)
{
    RUN_TEST(test_scheduler_periods_and_overruns);
}

/* ---- log ------------------------------------------------------------------- */

static char     s_log_buf[256];
static uint32_t s_log_ms;

static void log_capture(const char *s, size_t len)
{
    memcpy(s_log_buf, s, len);
    s_log_buf[len] = '\0';
}
static uint32_t log_clock(void) { return s_log_ms; }

static void test_log_format_and_level(void)
{
    log_init(log_capture, log_clock);
    log_set_level(LOG_LEVEL_INFO);
    s_log_ms = 12345;
    LOG_W("app", "jam %d", 3);
    CHECK_STR(s_log_buf, "[    12.345] W app: jam 3\r\n");
    s_log_buf[0] = '\0';
    LOG_D("app", "hidden");
    CHECK_STR(s_log_buf, "");

    log_level_t lvl;
    CHECK(log_level_parse("DEBUG", &lvl));
    CHECK_EQ(lvl, LOG_LEVEL_DEBUG);
    CHECK(!log_level_parse("verbose", &lvl));
}

static void test_log_truncates_long_lines(void)
{
    char long_msg[300];
    memset(long_msg, 'x', sizeof(long_msg) - 1);
    long_msg[sizeof(long_msg) - 1] = '\0';
    log_init(log_capture, log_clock);
    LOG_E("t", "%s", long_msg);
    size_t n = strlen(s_log_buf);
    CHECK(n < LOG_LINE_MAX);
    CHECK(strcmp(&s_log_buf[n - 5], "...\r\n") == 0);
    log_init(NULL, NULL);
}

void suite_log(void)
{
    RUN_TEST(test_log_format_and_level);
    RUN_TEST(test_log_truncates_long_lines);
}
