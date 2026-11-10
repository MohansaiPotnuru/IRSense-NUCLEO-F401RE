/**
 * @file    test_config.c
 * @brief   Tests for the config table and the wear-levelled flash store,
 *          run against a simulated NOR flash (erase = 0xFF, program can
 *          only clear bits, optional fault injection).
 */
#include "test.h"

#include "app/config.h"
#include "app/config_store.h"

/* ---- simulated flash --------------------------------------------------- */

#define FAKE_FLASH_SIZE 1024u

typedef struct {
    uint8_t  mem[FAKE_FLASH_SIZE];
    uint32_t erases;
    int      fail_program_after; /* words; -1 = never fail */
    int      programmed_words;
    int      bit_errors;         /* writes that tried to set a 0 bit to 1 */
} fake_flash_t;

static bool ff_read(void *ctx, uint32_t off, void *dst, uint32_t len)
{
    fake_flash_t *f = ctx;
    if (off + len > FAKE_FLASH_SIZE) {
        return false;
    }
    memcpy(dst, &f->mem[off], len);
    return true;
}

static bool ff_program(void *ctx, uint32_t off, const void *src, uint32_t len)
{
    fake_flash_t  *f = ctx;
    const uint8_t *p = src;
    if ((off % 4u) != 0u || (len % 4u) != 0u || off + len > FAKE_FLASH_SIZE) {
        return false;
    }
    for (uint32_t i = 0; i < len; i++) {
        if (i % 4u == 0u) {
            if (f->fail_program_after >= 0 && f->programmed_words >= f->fail_program_after) {
                return false; /* simulated power loss mid-write */
            }
            f->programmed_words++;
        }
        if ((p[i] & ~f->mem[off + i]) != 0u) {
            f->bit_errors++;
        }
        f->mem[off + i] &= p[i]; /* NOR: can only clear bits */
    }
    return true;
}

static bool ff_erase(void *ctx)
{
    fake_flash_t *f = ctx;
    memset(f->mem, 0xFF, sizeof(f->mem));
    f->erases++;
    return true;
}

static fake_flash_t   s_flash;
static flash_region_t s_region;

static void ff_reset(void)
{
    memset(&s_flash, 0, sizeof(s_flash));
    memset(s_flash.mem, 0xFF, sizeof(s_flash.mem));
    s_flash.fail_program_after = -1;
    s_region.size    = FAKE_FLASH_SIZE;
    s_region.read    = ff_read;
    s_region.program = ff_program;
    s_region.erase   = ff_erase;
    s_region.ctx     = &s_flash;
}

/* ---- config table -------------------------------------------------------- */

static void test_config_defaults_valid(void)
{
    config_t c;
    config_set_defaults(&c);
    CHECK(config_is_valid(&c));
    CHECK_EQ(c.debounce_us, 2000);
    CHECK_EQ(c.batch_target, 10);
    CHECK_EQ(c.ir_active_low, 1);
}

static void test_config_set_get_and_ranges(void)
{
    config_t c;
    config_set_defaults(&c);
    CHECK_EQ(config_set(&c, "BATCH", 25), CONFIG_OK); /* case-insensitive */
    CHECK_EQ(c.batch_target, 25);
    CHECK_EQ(config_set(&c, "debounce_us", 10), CONFIG_ERR_RANGE);
    CHECK_EQ(c.debounce_us, 2000);
    CHECK_EQ(config_set(&c, "nope", 1), CONFIG_ERR_UNKNOWN);
    const config_param_t *p = config_param_find("jam_ms");
    CHECK(p != NULL);
    CHECK_EQ(config_get(&c, p), 3000);
    CHECK(config_param_at(config_param_count()) == NULL);

    c.log_level = 9;
    CHECK(!config_is_valid(&c));
}

void suite_config(void)
{
    RUN_TEST(test_config_defaults_valid);
    RUN_TEST(test_config_set_get_and_ranges);
}

/* ---- config store -------------------------------------------------------- */

static void test_store_empty_then_roundtrip(void)
{
    config_t            a, b;
    config_store_info_t info;
    ff_reset();
    config_set_defaults(&a);
    CHECK_EQ(config_store_load(&s_region, &b, &info), CFG_STORE_EMPTY);
    CHECK_EQ(info.slots_used, 0);

    a.batch_target = 42;
    CHECK_EQ(config_store_save(&s_region, &a, &info), CFG_STORE_OK);
    CHECK_EQ(info.last_seq, 1);
    CHECK_EQ(info.slots_used, 1);
    CHECK_EQ(config_store_load(&s_region, &b, &info), CFG_STORE_OK);
    CHECK_EQ(b.batch_target, 42);
    CHECK_EQ(s_flash.bit_errors, 0);
}

static void test_store_appends_and_wraps_with_erase(void)
{
    config_t            a, b;
    config_store_info_t info;
    ff_reset();
    config_set_defaults(&a);
    const uint32_t slots = FAKE_FLASH_SIZE / config_store_slot_size();

    for (uint32_t i = 1; i <= slots; i++) {
        a.batch_target = i;
        CHECK_EQ(config_store_save(&s_region, &a, &info), CFG_STORE_OK);
    }
    CHECK_EQ(s_flash.erases, 0); /* filled every slot without erasing */
    CHECK_EQ(info.slots_used, slots);

    a.batch_target = 999;
    CHECK_EQ(config_store_save(&s_region, &a, &info), CFG_STORE_OK);
    CHECK_EQ(s_flash.erases, 1);
    CHECK_EQ(info.erases, 1);
    CHECK_EQ(info.slots_used, 1);
    CHECK_EQ(info.last_seq, slots + 1);
    CHECK_EQ(config_store_load(&s_region, &b, &info), CFG_STORE_OK);
    CHECK_EQ(b.batch_target, 999);
    CHECK_EQ(s_flash.bit_errors, 0);
}

static void test_store_survives_torn_write(void)
{
    config_t            a, b;
    config_store_info_t info;
    ff_reset();
    config_set_defaults(&a);
    a.batch_target = 7;
    CHECK_EQ(config_store_save(&s_region, &a, &info), CFG_STORE_OK);

    /* Power fails half way through the next record. */
    s_flash.fail_program_after = s_flash.programmed_words + 5;
    a.batch_target = 8;
    CHECK_EQ(config_store_save(&s_region, &a, &info), CFG_STORE_IO_ERROR);
    s_flash.fail_program_after = -1;

    CHECK_EQ(config_store_load(&s_region, &b, &info), CFG_STORE_OK);
    CHECK_EQ(b.batch_target, 7); /* old settings intact */
    CHECK_EQ(info.slots_used, 2);

    /* The next save skips the damaged slot. */
    a.batch_target = 9;
    CHECK_EQ(config_store_save(&s_region, &a, &info), CFG_STORE_OK);
    CHECK_EQ(info.slots_used, 3);
    CHECK_EQ(config_store_load(&s_region, &b, &info), CFG_STORE_OK);
    CHECK_EQ(b.batch_target, 9);
    CHECK_EQ(s_flash.bit_errors, 0);
}

static void test_store_rejects_corruption_and_invalid(void)
{
    config_t            a, b;
    config_store_info_t info;
    ff_reset();
    config_set_defaults(&a);
    CHECK_EQ(config_store_save(&s_region, &a, &info), CFG_STORE_OK);
    s_flash.mem[16] ^= 0x01u; /* flip one bit inside the config payload */
    CHECK_EQ(config_store_load(&s_region, &b, &info), CFG_STORE_EMPTY);

    a.boot_mode = 77;
    CHECK_EQ(config_store_save(&s_region, &a, &info), CFG_STORE_INVALID);
}

void suite_config_store(void)
{
    RUN_TEST(test_store_empty_then_roundtrip);
    RUN_TEST(test_store_appends_and_wraps_with_erase);
    RUN_TEST(test_store_survives_torn_write);
    RUN_TEST(test_store_rejects_corruption_and_invalid);
}
