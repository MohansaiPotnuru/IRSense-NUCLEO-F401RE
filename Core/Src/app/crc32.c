/**
 * @file    crc32.c
 * @brief   Bitwise CRC-32. Small and table-free; only used for config
 *          records, so speed doesn't matter.
 */
#include "app/crc32.h"

uint32_t crc32_update(uint32_t crc, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    crc = ~crc;
    while (len-- > 0u) {
        crc ^= *p++;
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

uint32_t crc32(const void *data, size_t len)
{
    return crc32_update(0u, data, len);
}
