/**
 * @file    cli.h
 * @brief   Line-oriented command shell for a serial terminal.
 *
 * Features: echo, backspace, Ctrl-C, one-line history (up arrow),
 * "double quoted" arguments, case-insensitive command names, a built-in
 * `help`, and table-driven commands.
 */
#ifndef APP_CLI_H
#define APP_CLI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CLI_LINE_MAX 96u
#define CLI_ARGV_MAX 8

enum {
    CLI_OK           = 0,
    CLI_ERR_UNKNOWN  = -1, /* no such command        */
    CLI_ERR_ARGS     = -2, /* wrong arguments        */
    CLI_ERR_RANGE    = -3, /* value out of range     */
    CLI_ERR_FAIL     = -4, /* command failed         */
    CLI_ERR_SYNTAX   = -5, /* bad quoting / too many */
};

struct cli;
typedef int  (*cli_handler_t)(struct cli *cli, int argc, char **argv);
typedef void (*cli_write_t)(const char *s, size_t len);

typedef struct {
    const char   *name;
    const char   *args; /* usage, e.g. "<name> <value>" */
    const char   *help;
    cli_handler_t handler;
} cli_command_t;

typedef struct cli {
    const cli_command_t *cmds;
    size_t               ncmds;
    cli_write_t          write;
    const char          *prompt;
    void                *ctx;
    char                 line[CLI_LINE_MAX];
    size_t               len;
    char                 history[CLI_LINE_MAX];
    uint8_t              esc_state;
    char                 last_char;
    int                  last_result;
} cli_t;

void        cli_init(cli_t *cli, const cli_command_t *cmds, size_t ncmds, cli_write_t write,
                     const char *prompt, void *ctx);
void        cli_feed(cli_t *cli, char c);
int         cli_execute(cli_t *cli, const char *line);
int         cli_tokenize(char *line, char **argv, int max_args);
void        cli_prompt(cli_t *cli);
void        cli_print(cli_t *cli, const char *s);
#if defined(__GNUC__)
__attribute__((format(printf, 2, 3)))
#endif
void        cli_printf(cli_t *cli, const char *fmt, ...);
bool        cli_parse_u32(const char *s, uint32_t *out);
bool        cli_parse_bool(const char *s, bool *out);
const char *cli_strerror(int err);

#ifdef __cplusplus
}
#endif

#endif /* APP_CLI_H */
