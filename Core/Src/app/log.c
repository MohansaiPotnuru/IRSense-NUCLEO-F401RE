/**
 * @file    log.c
 * @brief   Leveled, timestamped logging to a pluggable sink.
 */
#include "app/log.h"
#include "app/util.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static log_sink_t  s_sink;
static uint32_t  (*s_clock_ms)(void);
static log_level_t s_level = LOG_LEVEL_INFO;
static uint32_t    s_lines;

static const char *const k_names[LOG_LEVEL_NUM] = { "error", "warn", "info", "debug" };
static const char k_letters[LOG_LEVEL_NUM] = { 'E', 'W', 'I', 'D' };

void log_init(log_sink_t sink, uint32_t (*clock_ms)(void))
{
    s_sink     = sink;
    s_clock_ms = clock_ms;
    s_lines    = 0u;
}

void log_set_level(log_level_t level)
{
    if (level < LOG_LEVEL_NUM) {
        s_level = level;
    }
}

log_level_t log_get_level(void)
{
    return s_level;
}

const char *log_level_name(log_level_t level)
{
    return (level < LOG_LEVEL_NUM) ? k_names[level] : "?";
}

bool log_level_parse(const char *s, log_level_t *out)
{
    for (unsigned i = 0u; i < (unsigned)LOG_LEVEL_NUM; i++) {
        if (str_ieq(s, k_names[i])) {
            *out = (log_level_t)i;
            return true;
        }
    }
    return false;
}

uint32_t log_lines_written(void)
{
    return s_lines;
}

void log_write(log_level_t level, const char *tag, const char *fmt, ...)
{
    if (s_sink == NULL || level >= LOG_LEVEL_NUM || level > s_level) {
        return;
    }

    char     line[LOG_LINE_MAX];
    uint32_t ms = (s_clock_ms != NULL) ? s_clock_ms() : 0u;
    int      n  = snprintf(line, sizeof(line), "[%6lu.%03lu] %c %s: ",
                           (unsigned long)(ms / 1000u), (unsigned long)(ms % 1000u),
                           k_letters[level], (tag != NULL) ? tag : "-");
    if (n < 0) {
        return;
    }
    size_t len = (size_t)n;
    /* Leave room for "\r\n". */
    const size_t body_max = sizeof(line) - 2u;

    if (len < body_max) {
        va_list ap;
        va_start(ap, fmt);
        int m = vsnprintf(&line[len], body_max - len, fmt, ap);
        va_end(ap);
        if (m > 0) {
            if ((size_t)m >= body_max - len) {
                len = body_max - 1u; /* truncated; mark it */
                memcpy(&line[len - 3u], "...", 3u);
            } else {
                len += (size_t)m;
            }
        }
    } else {
        len = body_max - 1u;
    }
    line[len++] = '\r';
    line[len++] = '\n';
    s_sink(line, len);
    s_lines++;
}
