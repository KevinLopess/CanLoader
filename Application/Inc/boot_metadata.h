#ifndef BOOT_METADATA_H
#define BOOT_METADATA_H

#include <stdbool.h>
#include <stdint.h>

#include "boot_flash.h"

typedef struct
{
    uint32_t image_size;
    uint32_t image_crc32;
    uint32_t image_version;
    uint32_t state;
} boot_metadata_t;

bool boot_metadata_read(boot_metadata_t *metadata);
boot_flash_status_t boot_metadata_begin(uint32_t image_size, uint32_t image_crc32);
boot_flash_status_t boot_metadata_mark_valid(void);
boot_flash_status_t boot_metadata_mark_invalid(void);

#endif /* BOOT_METADATA_H */
