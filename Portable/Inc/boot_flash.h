#ifndef BOOT_FLASH_H
#define BOOT_FLASH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum
{
    BOOT_FLASH_REGION_APPLICATION = 0,
    BOOT_FLASH_REGION_METADATA,
    BOOT_FLASH_REGION_DEVICE_CONFIG
} boot_flash_region_t;

typedef enum
{
    BOOT_FLASH_STATUS_OK = 0,
    BOOT_FLASH_STATUS_INVALID_ARGUMENT,
    BOOT_FLASH_STATUS_ALIGNMENT,
    BOOT_FLASH_STATUS_RANGE,
    BOOT_FLASH_STATUS_ERASE,
    BOOT_FLASH_STATUS_PROGRAM,
    BOOT_FLASH_STATUS_VERIFY,
    BOOT_FLASH_STATUS_HARDWARE
} boot_flash_status_t;

boot_flash_status_t boot_flash_init(void);
bool boot_flash_is_valid_range(boot_flash_region_t region, uint32_t offset, size_t length);
boot_flash_status_t boot_flash_erase(boot_flash_region_t region, uint32_t offset, size_t length);
boot_flash_status_t boot_flash_write(boot_flash_region_t region,
                                     uint32_t offset,
                                     const uint8_t *data,
                                     size_t length);
boot_flash_status_t boot_flash_read(boot_flash_region_t region,
                                    uint32_t offset,
                                    uint8_t *data,
                                    size_t length);

#endif /* BOOT_FLASH_H */
