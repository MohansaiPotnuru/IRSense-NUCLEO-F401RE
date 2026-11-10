/**
 * @file    test_main.c
 * @brief   Runs every host unit-test suite. Exit code 0 = all passed.
 */
#include "test.h"

int g_test_checks;
int g_test_failures;

typedef struct {
    const char *name;
    void (*run)(void);
} suite_t;

static const suite_t k_suites[] = {
    { "ring_buffer", suite_ring_buffer },
    { "crc32", suite_crc32 },
    { "event_queue", suite_event_queue },
    { "edge_filter", suite_edge_filter },
    { "button", suite_button },
    { "stats", suite_stats },
    { "led_pattern", suite_led_pattern },
    { "scheduler", suite_scheduler },
    { "log", suite_log },
    { "config", suite_config },
    { "config_store", suite_config_store },
    { "cli", suite_cli },
    { "app", suite_app },
    { "app_cli", suite_app_cli },
};

int main(void)
{
    for (size_t i = 0; i < sizeof(k_suites) / sizeof(k_suites[0]); i++) {
        printf("[%s]\n", k_suites[i].name);
        k_suites[i].run();
    }
    printf("\n%d checks, %d failed\n", g_test_checks, g_test_failures);
    return (g_test_failures == 0) ? 0 : 1;
}
