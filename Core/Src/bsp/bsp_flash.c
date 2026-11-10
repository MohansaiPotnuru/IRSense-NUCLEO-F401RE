/**
 * @file    bsp_flash.c
 * @brief   flash_region_t driver for STM32F401 flash sector 7.
 *
 * Sector 7 = 0x08060000..0x0807FFFF (128 KiB). The linker script limits
 * the program to the first 384 KiB so code never lands here.
 *
 * Note: the F401 has a single flash bank, so the CPU stalls while a sector
 * erases (~1-2 s). The watchdog is kicked just before; interrupts stay
 * pending and run afterwards (IR timestamps taken during an erase are late).
 */
#include "bsp/bsp.h"
#include "main.h"

#include <string.h>

#define CFG_FLASH_BASE   0x08060000u
#define CFG_FLASH_SIZE   (128u * 1024u)
#define CFG_FLASH_SECTOR FLASH_SECTOR_7

static void clear_flash_errors(void)
{
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                           FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
#ifdef FLASH_FLAG_RDERR
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_RDERR);
#endif
}

static void flush_data_cache(void)
{
    /* Make sure later reads see the new flash contents, not cached lines. */
    __HAL_FLASH_DATA_CACHE_DISABLE();
    __HAL_FLASH_DATA_CACHE_RESET();
    __HAL_FLASH_DATA_CACHE_ENABLE();
}

static bool fl_read(void *ctx, uint32_t offset, void *dst, uint32_t len)
{
    (void)ctx;
    if (offset > CFG_FLASH_SIZE || len > CFG_FLASH_SIZE - offset) {
        return false;
    }
    memcpy(dst, (const void *)(CFG_FLASH_BASE + offset), len);
    return true;
}

static bool fl_program(void *ctx, uint32_t offset, const void *src, uint32_t len)
{
    (void)ctx;
    if ((offset % 4u) != 0u || (len % 4u) != 0u || offset > CFG_FLASH_SIZE ||
        len > CFG_FLASH_SIZE - offset) {
        return false;
    }

    bool ok = (HAL_FLASH_Unlock() == HAL_OK);
    clear_flash_errors();
    const uint8_t *p = (const uint8_t *)src;
    for (uint32_t i = 0u; ok && i < len; i += 4u) {
        uint32_t word;
        memcpy(&word, &p[i], sizeof(word));
        ok = (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, CFG_FLASH_BASE + offset + i, word) ==
              HAL_OK);
    }
    (void)HAL_FLASH_Lock();
    flush_data_cache();
    return ok;
}

static bool fl_erase(void *ctx)
{
    (void)ctx;
    FLASH_EraseInitTypeDef erase = { 0 };
    uint32_t               sector_error = 0u;

    erase.TypeErase    = FLASH_TYPEERASE_SECTORS;
    erase.Sector       = CFG_FLASH_SECTOR;
    erase.NbSectors    = 1u;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3; /* 2.7-3.6 V: 32-bit parallelism */

    bsp_wdg_kick();
    bool ok = (HAL_FLASH_Unlock() == HAL_OK);
    clear_flash_errors();
    if (ok) {
        ok = (HAL_FLASHEx_Erase(&erase, &sector_error) == HAL_OK) && sector_error == 0xFFFFFFFFu;
    }
    (void)HAL_FLASH_Lock();
    flush_data_cache();
    bsp_wdg_kick();
    return ok;
}

static const flash_region_t s_region = {
    .size    = CFG_FLASH_SIZE,
    .read    = fl_read,
    .program = fl_program,
    .erase   = fl_erase,
    .ctx     = NULL,
};

const flash_region_t *bsp_config_flash(void)
{
    return &s_region;
}
