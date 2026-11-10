/**
 * @file    config.c
 * @brief   Runtime-tunable settings, described by a parameter table.
 */
#include "app/config.h"
#include "app/util.h"

#include <string.h>

#define PARAM(field, nm, lo, hi, dflt, u, h) \
    { (nm), (uint32_t)offsetof(config_t, field), (lo), (hi), (dflt), (u), (h) }

static const config_param_t k_params[] = {
    PARAM(debounce_us,        "debounce_us",  50u,  100000u,   2000u, "us",
          "IR glitch filter: level must be stable this long"),
    PARAM(min_width_us,       "min_width_us", 0u,   10000000u, 0u,    "us",
          "objects shorter than this are rejected"),
    PARAM(jam_timeout_ms,     "jam_ms",       0u,   600000u,   3000u, "ms",
          "beam blocked longer than this raises JAM (0 = off)"),
    PARAM(batch_target,       "batch",        0u,   1000000u,  10u,   "obj",
          "objects per batch in counter mode (0 = off)"),
    PARAM(rate_alarm_per_min, "rate_alarm",   0u,   60000u,    0u,    "/min",
          "rate that raises the RATE alarm (0 = off)"),
    PARAM(ir_active_low,      "active_low",   0u,   1u,        1u,    "",
          "1 = sensor output is LOW when an object is present"),
    PARAM(stream,             "stream",       0u,   1u,        0u,    "",
          "1 = print a $-record for every event"),
    PARAM(log_level,          "log_level",    0u,   3u,        2u,    "",
          "0 error, 1 warn, 2 info, 3 debug"),
    PARAM(boot_mode,          "boot_mode",    0u,   2u,        0u,    "",
          "mode after reset: 0 counter, 1 monitor, 2 guard"),
};

_Static_assert(sizeof(config_t) == sizeof(k_params) / sizeof(k_params[0]) * sizeof(uint32_t),
               "every config_t field needs exactly one row in k_params");

static uint32_t *field_ptr(config_t *cfg, const config_param_t *p)
{
    return (uint32_t *)(void *)((uint8_t *)cfg + p->offset);
}

static const uint32_t *field_cptr(const config_t *cfg, const config_param_t *p)
{
    return (const uint32_t *)(const void *)((const uint8_t *)cfg + p->offset);
}

size_t config_param_count(void)
{
    return ARRAY_SIZE(k_params);
}

const config_param_t *config_param_at(size_t index)
{
    return (index < ARRAY_SIZE(k_params)) ? &k_params[index] : NULL;
}

const config_param_t *config_param_find(const char *name)
{
    for (size_t i = 0u; i < ARRAY_SIZE(k_params); i++) {
        if (str_ieq(name, k_params[i].name)) {
            return &k_params[i];
        }
    }
    return NULL;
}

void config_set_defaults(config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    for (size_t i = 0u; i < ARRAY_SIZE(k_params); i++) {
        *field_ptr(cfg, &k_params[i]) = k_params[i].def;
    }
}

bool config_is_valid(const config_t *cfg)
{
    for (size_t i = 0u; i < ARRAY_SIZE(k_params); i++) {
        uint32_t v = *field_cptr(cfg, &k_params[i]);
        if (v < k_params[i].min || v > k_params[i].max) {
            return false;
        }
    }
    return true;
}

uint32_t config_get(const config_t *cfg, const config_param_t *p)
{
    return *field_cptr(cfg, p);
}

config_status_t config_set(config_t *cfg, const char *name, uint32_t value)
{
    const config_param_t *p = config_param_find(name);
    if (p == NULL) {
        return CONFIG_ERR_UNKNOWN;
    }
    if (value < p->min || value > p->max) {
        return CONFIG_ERR_RANGE;
    }
    *field_ptr(cfg, p) = value;
    return CONFIG_OK;
}
