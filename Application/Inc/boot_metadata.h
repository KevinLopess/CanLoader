#ifndef BOOT_METADATA_H
#define BOOT_METADATA_H

#include <stdbool.h>
#include <stdint.h>

#include "boot_flash.h"

typedef enum
{
    BOOT_METADATA_STATE_UPDATING = 0,
    BOOT_METADATA_STATE_VALID,
    BOOT_METADATA_STATE_INVALID
} boot_metadata_state_t;

typedef struct
{
    uint32_t image_size;
    uint32_t image_crc32;
    uint32_t image_version;
    boot_metadata_state_t state;
} boot_metadata_t;

bool boot_metadata_read(boot_metadata_t *metadata);
boot_flash_status_t boot_metadata_begin(uint32_t image_size, uint32_t image_crc32);
boot_flash_status_t boot_metadata_mark_valid(void);
boot_flash_status_t boot_metadata_mark_invalid(void);

#endif /* BOOT_METADATA_H */
