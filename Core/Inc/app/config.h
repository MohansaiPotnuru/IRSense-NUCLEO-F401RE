/**
 * @file    config.h
 * @brief   Runtime-tunable settings, described by a parameter table.
 *
 * Every field is a uint32_t so a single table (name, offset, range,
 * default, help) drives defaults, validation and the CLI `set`/`get`
 * commands. Adding a setting = add a field + one table row.
 */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t debounce_us;        /* IR glitch filter stability time           */
    uint32_t min_width_us;       /* shorter objects are rejected as noise      */
    uint32_t jam_timeout_ms;     /* beam blocked longer than this -> JAM       */
    uint32_t batch_target;       /* objects per batch in COUNTER mode, 0 = off */
    uint32_t rate_alarm_per_min; /* RATE alarm threshold, 0 = off              */
    uint32_t ir_active_low;      /* 1: sensor output is LOW when blocked       */
    uint32_t stream;             /* 1: emit $-records on the serial port       */
    uint32_t log_level;          /* 0 error .. 3 debug                         */
    uint32_t boot_mode;          /* mode after reset: 0 counter, 1 monitor, 2 guard */
} config_t;

typedef struct {
    const char *name;
    uint32_t    offset; /* offsetof(config_t, field) */
    uint32_t    min;
    uint32_t    max;
    uint32_t    def;
    const char *unit;
    const char *help;
} config_param_t;

typedef enum {
    CONFIG_OK = 0,
    CONFIG_ERR_UNKNOWN,
    CONFIG_ERR_RANGE,
} config_status_t;

size_t                config_param_count(void);
const config_param_t *config_param_at(size_t index);
const config_param_t *config_param_find(const char *name); /* case-insensitive */

void            config_set_defaults(config_t *cfg);
bool            config_is_valid(const config_t *cfg);
uint32_t        config_get(const config_t *cfg, const config_param_t *p);
config_status_t config_set(config_t *cfg, const char *name, uint32_t value);

#ifdef __cplusplus
}
#endif

#endif /* APP_CONFIG_H */
