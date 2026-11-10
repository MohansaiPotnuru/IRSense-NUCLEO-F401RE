/**
 * @file    log.h
 * @brief   Leveled, timestamped logging to a pluggable sink.
 *
 * Output format:  [   12.345] W app: beam blocked for 3000 ms (JAM)
 * Must only be called from the main loop, never from an interrupt.
 */
#ifndef APP_LOG_H
#define APP_LOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LOG_LEVEL_ERROR = 0,
    LOG_LEVEL_WARN  = 1,
    LOG_LEVEL_INFO  = 2,
    LOG_LEVEL_DEBUG = 3,
    LOG_LEVEL_NUM
} log_level_t;

#define LOG_LINE_MAX 160u

typedef void (*log_sink_t)(const char *s, size_t len);

void        log_init(log_sink_t sink, uint32_t (*clock_ms)(void));
void        log_set_level(log_level_t level);
log_level_t log_get_level(void);
const char *log_level_name(log_level_t level);
bool        log_level_parse(const char *s, log_level_t *out);
uint32_t    log_lines_written(void);

#if defined(__GNUC__)
__attribute__((format(printf, 3, 4)))
#endif
void log_write(log_level_t level, const char *tag, const char *fmt, ...);

#define LOG_E(tag, ...) log_write(LOG_LEVEL_ERROR, (tag), __VA_ARGS__)
#define LOG_W(tag, ...) log_write(LOG_LEVEL_WARN, (tag), __VA_ARGS__)
#define LOG_I(tag, ...) log_write(LOG_LEVEL_INFO, (tag), __VA_ARGS__)
#define LOG_D(tag, ...) log_write(LOG_LEVEL_DEBUG, (tag), __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* APP_LOG_H */
