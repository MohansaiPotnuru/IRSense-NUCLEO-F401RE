/**
 * @file    config_store.c
 * @brief   Wear-levelled, power-fail-safe config storage in a flash region.
 */
#include "app/config_store.h"
#include "app/crc32.h"

#include <stddef.h>
#include <string.h>

_Static_assert(sizeof(config_record_t) % 4u == 0u, "record must be word sized");
_Static_assert(offsetof(config_record_t, crc) == sizeof(config_record_t) - 4u,
               "crc must be the last field (no padding)");

#define LAYOUT_WORD ((CONFIG_RECORD_VERSION << 16) | (uint32_t)sizeof(config_t))
#define SLOT_SIZE   ((uint32_t)sizeof(config_record_t))

static uint32_t record_crc(const config_record_t *r)
{
    return crc32(r, offsetof(config_record_t, crc));
}

static bool record_valid(const config_record_t *r)
{
    return r->magic == CONFIG_RECORD_MAGIC && r->layout == LAYOUT_WORD &&
           r->crc == record_crc(r) && config_is_valid(&r->cfg);
}

static bool slot_is_erased(const flash_region_t *fr, uint32_t slot, bool *io_ok)
{
    uint32_t words[SLOT_SIZE / 4u];
    *io_ok = fr->read(fr->ctx, slot * SLOT_SIZE, words, SLOT_SIZE);
    if (!*io_ok) {
        return false;
    }
    for (size_t i = 0u; i < SLOT_SIZE / 4u; i++) {
        if (words[i] != 0xFFFFFFFFu) {
            return false;
        }
    }
    return true;
}

uint32_t config_store_slot_size(void)
{
    return SLOT_SIZE;
}

/* Scans the region. used = index one past the last non-erased slot. */
static cfg_store_status_t scan(const flash_region_t *fr, config_record_t *best,
                               bool *found, uint32_t *used)
{
    const uint32_t slots = fr->size / SLOT_SIZE;
    *found = false;
    *used  = 0u;

    for (uint32_t i = 0u; i < slots; i++) {
        config_record_t r;
        if (!fr->read(fr->ctx, i * SLOT_SIZE, &r, SLOT_SIZE)) {
            return CFG_STORE_IO_ERROR;
        }
        if (r.magic == 0xFFFFFFFFu) {
            bool io_ok;
            if (slot_is_erased(fr, i, &io_ok)) {
                break; /* records are appended in order: rest is free */
            }
            if (!io_ok) {
                return CFG_STORE_IO_ERROR;
            }
        }
        *used = i + 1u;
        if (record_valid(&r) && (!*found || (int32_t)(r.seq - best->seq) > 0)) {
            *best  = r;
            *found = true;
        }
    }
    return CFG_STORE_OK;
}

static void fill_info(config_store_info_t *info, const flash_region_t *fr, uint32_t used,
                      uint32_t seq, uint32_t erases)
{
    if (info != NULL) {
        info->slot_count = fr->size / SLOT_SIZE;
        info->slots_used = used;
        info->last_seq   = seq;
        info->erases     = erases;
    }
}

cfg_store_status_t config_store_load(const flash_region_t *fr, config_t *out,
                                     config_store_info_t *info)
{
    config_record_t best;
    bool            found;
    uint32_t        used;

    cfg_store_status_t st = scan(fr, &best, &found, &used);
    fill_info(info, fr, used, found ? best.seq : 0u, 0u);
    if (st != CFG_STORE_OK) {
        return st;
    }
    if (!found) {
        return CFG_STORE_EMPTY;
    }
    *out = best.cfg;
    return CFG_STORE_OK;
}

cfg_store_status_t config_store_save(const flash_region_t *fr, const config_t *cfg,
                                     config_store_info_t *info)
{
    if (!config_is_valid(cfg)) {
        return CFG_STORE_INVALID;
    }

    config_record_t best;
    bool            found;
    uint32_t        used;
    uint32_t        erases = 0u;
    const uint32_t  slots  = fr->size / SLOT_SIZE;

    cfg_store_status_t st = scan(fr, &best, &found, &used);
    if (st != CFG_STORE_OK) {
        return st;
    }

    /* Find the first fully erased slot at or after `used`. */
    uint32_t slot = used;
    while (slot < slots) {
        bool io_ok;
        if (slot_is_erased(fr, slot, &io_ok)) {
            break;
        }
        if (!io_ok) {
            return CFG_STORE_IO_ERROR;
        }
        slot++;
    }
    if (slot >= slots) {
        if (!fr->erase(fr->ctx)) {
            return CFG_STORE_IO_ERROR;
        }
        erases++;
        slot = 0u;
    }

    config_record_t r;
    memset(&r, 0, sizeof(r));
    r.magic  = CONFIG_RECORD_MAGIC;
    r.seq    = found ? best.seq + 1u : 1u;
    r.layout = LAYOUT_WORD;
    r.cfg    = *cfg;
    r.crc    = record_crc(&r);

    if (!fr->program(fr->ctx, slot * SLOT_SIZE, &r, SLOT_SIZE)) {
        fill_info(info, fr, slot + 1u, r.seq, erases);
        return CFG_STORE_IO_ERROR;
    }

    config_record_t check;
    if (!fr->read(fr->ctx, slot * SLOT_SIZE, &check, SLOT_SIZE)) {
        return CFG_STORE_IO_ERROR;
    }
    fill_info(info, fr, slot + 1u, r.seq, erases);
    return (memcmp(&check, &r, SLOT_SIZE) == 0) ? CFG_STORE_OK : CFG_STORE_VERIFY_ERROR;
}

cfg_store_status_t config_store_erase(const flash_region_t *fr)
{
    return fr->erase(fr->ctx) ? CFG_STORE_OK : CFG_STORE_IO_ERROR;
}

const char *config_store_status_name(cfg_store_status_t st)
{
    switch (st) {
    case CFG_STORE_OK:           return "ok";
    case CFG_STORE_EMPTY:        return "empty";
    case CFG_STORE_IO_ERROR:     return "flash I/O error";
    case CFG_STORE_VERIFY_ERROR: return "verify failed";
    case CFG_STORE_INVALID:      return "invalid config";
    default:                     return "?";
    }
}
