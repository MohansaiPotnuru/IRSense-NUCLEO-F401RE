/**
 * @file    app_cli.h
 * @brief   IRSense serial commands (status, config, save, sim, ...).
 *
 * Hardware-specific commands (`sys`, `tasks`, `reboot`) are delegated to
 * callbacks in app_platform_t so this file stays portable and testable.
 */
#ifndef APP_APP_CLI_H
#define APP_APP_CLI_H

#include "app/app.h"
#include "app/cli.h"
#include "app/config.h"
#include "app/config_store.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t (*now_ms)(void);
    uint32_t (*now_us)(void);
    void     (*print_sys)(cli_t *cli);   /* optional */
    void     (*print_tasks)(cli_t *cli); /* optional */
    void     (*reboot)(void);            /* optional */
    void     (*config_changed)(void);    /* optional: apply live settings */
} app_platform_t;

typedef struct {
    app_t                *app;
    config_t             *cfg;
    const flash_region_t *store;
    const app_platform_t *plat;
} app_cli_ctx_t;

void app_cli_init(cli_t *cli, app_cli_ctx_t *ctx, cli_write_t write);

#ifdef __cplusplus
}
#endif

#endif /* APP_APP_CLI_H */
