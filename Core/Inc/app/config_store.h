/**
 * @file    config_store.h
 * @brief   Wear-levelled, power-fail-safe config storage in a flash region.
 *
 * The region is split into fixed-size slots. Each save appends a new
 * record (magic, sequence number, layout, config, CRC-32) in the next
 * erased slot; load picks the valid record with the highest sequence
 * number. The region is erased only when every slot is used, so a
 * 128 KiB sector holds 2520 saves per erase cycle.
 *
 * A save interrupted by a reset leaves a record with a bad CRC, which load
 * ignores, so the previous settings survive.
 *
 * The flash driver is abstracted (flash_region_t) so the logic is unit
 * tested on a PC against a simulated NOR flash.
 */
#ifndef APP_CONFIG_STORE_H
#define APP_CONFIG_STORE_H

#include <stdbool.h>
#include <stdint.h>

#include "app/config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CONFIG_RECORD_MAGIC   0x31535249u /* "IRS1" little-endian */
#define CONFIG_RECORD_VERSION 1u

typedef struct {
    uint32_t magic;
    uint32_t seq;
    uint32_t layout; /* (CONFIG_RECORD_VERSION << 16) | sizeof(config_t) */
    config_t cfg;
    uint32_t crc;    /* CRC-32 of all preceding bytes */
} config_record_t;

/* NOR-flash style region: erase sets every byte to 0xFF, program can only
 * clear bits. Offsets and lengths passed to program are multiples of 4. */
typedef struct {
    uint32_t size;
    bool   (*read)(void *ctx, uint32_t offset, void *dst, uint32_t len);
    bool   (*program)(void *ctx, uint32_t offset, const void *src, uint32_t len);
    bool   (*erase)(void *ctx);
    void    *ctx;
} flash_region_t;

typedef enum {
    CFG_STORE_OK = 0,
    CFG_STORE_EMPTY,        /* no valid record found */
    CFG_STORE_IO_ERROR,     /* driver read/program/erase failed */
    CFG_STORE_VERIFY_ERROR, /* read-back didn't match what was written */
    CFG_STORE_INVALID,      /* refused to save an out-of-range config */
} cfg_store_status_t;

typedef struct {
    uint32_t slot_count;
    uint32_t slots_used;
    uint32_t last_seq;
    uint32_t erases; /* erase cycles performed by this call */
} config_store_info_t;

uint32_t           config_store_slot_size(void);
cfg_store_status_t config_store_load(const flash_region_t *fr, config_t *out,
                                     config_store_info_t *info);
cfg_store_status_t config_store_save(const flash_region_t *fr, const config_t *cfg,
                                     config_store_info_t *info);
cfg_store_status_t config_store_erase(const flash_region_t *fr);
const char        *config_store_status_name(cfg_store_status_t st);

#ifdef __cplusplus
}
#endif

#endif /* APP_CONFIG_STORE_H */
