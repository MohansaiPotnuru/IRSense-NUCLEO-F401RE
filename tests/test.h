/**
 * @file    test.h
 * @brief   Minimal unit-test harness for the host build (no dependencies).
 */
#ifndef TEST_H
#define TEST_H

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

extern int g_test_checks;
extern int g_test_failures;

#define CHECK(cond)                                                              \
    do {                                                                         \
        g_test_checks++;                                                         \
        if (!(cond)) {                                                           \
            g_test_failures++;                                                   \
            printf("    %s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond);  \
        }                                                                        \
    } while (0)

#define CHECK_EQ(actual, expected)                                               \
    do {                                                                         \
        long long a_ = (long long)(actual);                                      \
        long long e_ = (long long)(expected);                                    \
        g_test_checks++;                                                         \
        if (a_ != e_) {                                                          \
            g_test_failures++;                                                   \
            printf("    %s:%d: %s == %lld, expected %lld\n", __FILE__, __LINE__, \
                   #actual, a_, e_);                                             \
        }                                                                        \
    } while (0)

#define CHECK_STR(actual, expected)                                              \
    do {                                                                         \
        const char *a_ = (actual);                                               \
        const char *e_ = (expected);                                             \
        g_test_checks++;                                                         \
        if (strcmp(a_, e_) != 0) {                                               \
            g_test_failures++;                                                   \
            printf("    %s:%d: %s == \"%s\", expected \"%s\"\n", __FILE__,       \
                   __LINE__, #actual, a_, e_);                                   \
        }                                                                        \
    } while (0)

#define RUN_TEST(fn)                                                             \
    do {                                                                         \
        int before_ = g_test_failures;                                           \
        fn();                                                                    \
        printf("  %s %s\n", (g_test_failures == before_) ? "ok  " : "FAIL", #fn); \
    } while (0)

/* Each test file exposes one suite runner. */
void suite_ring_buffer(void);
void suite_crc32(void);
void suite_event_queue(void);
void suite_edge_filter(void);
void suite_button(void);
void suite_stats(void);
void suite_led_pattern(void);
void suite_scheduler(void);
void suite_log(void);
void suite_config(void);
void suite_config_store(void);

#endif /* TEST_H */
