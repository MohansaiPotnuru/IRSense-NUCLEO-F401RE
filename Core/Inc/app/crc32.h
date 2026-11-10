/**
 * @file    crc32.h
 * @brief   CRC-32 (IEEE 802.3, the zlib/PNG polynomial 0xEDB88320).
 *
 * crc32("123456789") == 0xCBF43926. Calls chain:
 *   crc = crc32_update(0, a, na); crc = crc32_update(crc, b, nb);
 */
#ifndef APP_CRC32_H
#define APP_CRC32_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t crc32_update(uint32_t crc, const void *data, size_t len);
uint32_t crc32(const void *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* APP_CRC32_H */
