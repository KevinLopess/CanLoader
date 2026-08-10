#ifndef BOOT_CRC32_H
#define BOOT_CRC32_H

#include <stddef.h>
#include <stdint.h>

#define BOOT_CRC32_INITIAL_VALUE UINT32_C(0xFFFFFFFF)

uint32_t boot_crc32_update(uint32_t crc, const uint8_t *data, size_t length);
uint32_t boot_crc32_finalize(uint32_t crc);
uint32_t boot_crc32_compute(const uint8_t *data, size_t length);

#endif /* BOOT_CRC32_H */
