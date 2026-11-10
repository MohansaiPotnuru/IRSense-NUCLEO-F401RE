/**
 * @file    cli.c
 * @brief   Line-oriented command shell for a serial terminal.
 */
#include "app/cli.h"
#include "app/util.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

enum { ESC_NONE = 0, ESC_GOT_ESC, ESC_GOT_BRACKET };

void cli_init(cli_t *cli, const cli_command_t *cmds, size_t ncmds, cli_write_t write,
              const char *prompt, void *ctx)
{
    memset(cli, 0, sizeof(*cli));
    cli->cmds   = cmds;
    cli->ncmds  = ncmds;
    cli->write  = write;
    cli->prompt = (prompt != NULL) ? prompt : "> ";
    cli->ctx    = ctx;
}

void cli_print(cli_t *cli, const char *s)
{
    if (cli->write != NULL) {
        cli->write(s, strlen(s));
    }
}

void cli_printf(cli_t *cli, const char *fmt, ...)
{
    char    buf[160];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0 && cli->write != NULL) {
        size_t len = ((size_t)n < sizeof(buf)) ? (size_t)n : sizeof(buf) - 1u;
        cli->write(buf, len);
    }
}

void cli_prompt(cli_t *cli)
{
    cli_print(cli, cli->prompt);
}

const char *cli_strerror(int err)
{
    switch (err) {
    case CLI_OK:          return "ok";
    case CLI_ERR_UNKNOWN: return "unknown command (try 'help')";
    case CLI_ERR_ARGS:    return "bad arguments";
    case CLI_ERR_RANGE:   return "value out of range";
    case CLI_ERR_FAIL:    return "command failed";
    case CLI_ERR_SYNTAX:  return "syntax error (quotes or too many arguments)";
    default:              return "error";
    }
}

bool cli_parse_u32(const char *s, uint32_t *out)
{
    if (s == NULL || *s == '\0') {
        return false;
    }
    uint32_t base = 10u;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16u;
        s += 2;
        if (*s == '\0') {
            return false;
        }
    }
    uint64_t v = 0u;
    for (; *s != '\0'; s++) {
        uint32_t d;
        if (*s >= '0' && *s <= '9') {
            d = (uint32_t)(*s - '0');
        } else if (base == 16u && *s >= 'a' && *s <= 'f') {
            d = (uint32_t)(*s - 'a' + 10);
        } else if (base == 16u && *s >= 'A' && *s <= 'F') {
            d = (uint32_t)(*s - 'A' + 10);
        } else if (*s == '_') {
            continue; /* allow 1_000_000 */
        } else {
            return false;
        }
        v = v * base + d;
        if (v > UINT32_MAX) {
            return false;
        }
    }
    *out = (uint32_t)v;
    return true;
}

bool cli_parse_bool(const char *s, bool *out)
{
    if (str_ieq(s, "on") || str_ieq(s, "1") || str_ieq(s, "yes") || str_ieq(s, "true")) {
        *out = true;
        return true;
    }
    if (str_ieq(s, "off") || str_ieq(s, "0") || str_ieq(s, "no") || str_ieq(s, "false")) {
        *out = false;
        return true;
    }
    return false;
}

int cli_tokenize(char *line, char **argv, int max_args)
{
    int   argc = 0;
    char *p    = line;

    for (;;) {
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '\0') {
            return argc;
        }
        if (argc >= max_args) {
            return CLI_ERR_SYNTAX;
        }
        if (*p == '"') {
            p++;
            argv[argc++] = p;
            while (*p != '\0' && *p != '"') {
                p++;
            }
            if (*p != '"') {
                return CLI_ERR_SYNTAX; /* unterminated quote */
            }
            *p++ = '\0';
        } else {
            argv[argc++] = p;
            while (*p != '\0' && *p != ' ' && *p != '\t') {
                p++;
            }
            if (*p != '\0') {
                *p++ = '\0';
            }
        }
    }
}

static const cli_command_t *find_command(const cli_t *cli, const char *name)
{
    for (size_t i = 0u; i < cli->ncmds; i++) {
        if (str_ieq(name, cli->cmds[i].name)) {
            return &cli->cmds[i];
        }
    }
    return NULL;
}

static void print_usage(cli_t *cli, const cli_command_t *c)
{
    cli_printf(cli, "usage: %s %s\r\n", c->name, (c->args != NULL) ? c->args : "");
}

static int builtin_help(cli_t *cli, int argc, char **argv)
{
    if (argc >= 2) {
        const cli_command_t *c = find_command(cli, argv[1]);
        if (c == NULL) {
            return CLI_ERR_UNKNOWN;
        }
        print_usage(cli, c);
        cli_printf(cli, "  %s\r\n", c->help);
        return CLI_OK;
    }
    cli_print(cli, "Commands:\r\n");
    for (size_t i = 0u; i < cli->ncmds; i++) {
        const cli_command_t *c = &cli->cmds[i];
        char head[40];
        snprintf(head, sizeof(head), "%s %s", c->name, (c->args != NULL) ? c->args : "");
        cli_printf(cli, "  %-26s %s\r\n", head, c->help);
    }
    cli_print(cli, "  help [command]             show this list or one command\r\n");
    return CLI_OK;
}

int cli_execute(cli_t *cli, const char *line)
{
    char  buf[CLI_LINE_MAX];
    char *argv[CLI_ARGV_MAX];

    size_t n = strlen(line);
    if (n >= sizeof(buf)) {
        n = sizeof(buf) - 1u;
    }
    memcpy(buf, line, n);
    buf[n] = '\0';

    int argc = cli_tokenize(buf, argv, CLI_ARGV_MAX);
    int rc;
    const cli_command_t *c = NULL;

    if (argc < 0) {
        rc = argc;
    } else if (argc == 0) {
        rc = CLI_OK;
    } else if ((c = find_command(cli, argv[0])) != NULL) {
        rc = c->handler(cli, argc, argv);
    } else if (str_ieq(argv[0], "help") || str_ieq(argv[0], "?")) {
        rc = builtin_help(cli, argc, argv);
    } else {
        rc = CLI_ERR_UNKNOWN;
    }

    if (rc != CLI_OK) {
        cli_printf(cli, "error: %s\r\n", cli_strerror(rc));
        if (rc == CLI_ERR_ARGS && c != NULL) {
            print_usage(cli, c);
        }
    }
    cli->last_result = rc;
    return rc;
}

static void redraw_line(cli_t *cli)
{
    cli_print(cli, "\r\x1b[K");
    cli_prompt(cli);
    if (cli->write != NULL && cli->len > 0u) {
        cli->write(cli->line, cli->len);
    }
}

void cli_feed(cli_t *cli, char c)
{
    char prev      = cli->last_char;
    cli->last_char = c;

    /* ANSI escape sequences: only "ESC [ A" (up arrow) is used. */
    if (cli->esc_state == ESC_GOT_ESC) {
        cli->esc_state = (c == '[') ? ESC_GOT_BRACKET : ESC_NONE;
        return;
    }
    if (cli->esc_state == ESC_GOT_BRACKET) {
        cli->esc_state = ESC_NONE;
        if (c == 'A' && cli->history[0] != '\0') {
            cli->len = strlen(cli->history);
            memcpy(cli->line, cli->history, cli->len);
            redraw_line(cli);
        }
        return;
    }

    switch (c) {
    case 0x1B:
        cli->esc_state = ESC_GOT_ESC;
        return;

    case '\n':
        if (prev == '\r') {
            return; /* CR LF: already handled on CR */
        }
        /* fall through */
    case '\r':
        cli_print(cli, "\r\n");
        cli->line[cli->len] = '\0';
        if (cli->len > 0u) {
            memcpy(cli->history, cli->line, cli->len + 1u);
            cli_execute(cli, cli->line);
        }
        cli->len = 0u;
        cli_prompt(cli);
        return;

    case 0x08: /* backspace */
    case 0x7F: /* delete */
        if (cli->len > 0u) {
            cli->len--;
            cli_print(cli, "\b \b");
        }
        return;

    case 0x03: /* Ctrl-C */
        cli_print(cli, "^C\r\n");
        cli->len = 0u;
        cli_prompt(cli);
        return;

    default:
        if (c >= 0x20 && c <= 0x7E) {
            if (cli->len < CLI_LINE_MAX - 1u) {
                cli->line[cli->len++] = c;
                if (cli->write != NULL) {
                    cli->write(&c, 1u);
                }
            } else {
                cli_print(cli, "\a");
            }
        }
        return;
    }
}
