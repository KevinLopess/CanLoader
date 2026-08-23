#ifndef BOOT_DEVICE_CONFIG_H
#define BOOT_DEVICE_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#include "boot_device_config_format.h"
#include "boot_flash.h"

typedef struct
{
    uint64_t name;
    uint32_t generation;
    uint16_t flags;
    uint8_t preferred_address;
} boot_device_config_t;

void boot_device_config_defaults(boot_device_config_t *config);
bool boot_device_config_read(boot_device_config_t *config);
void boot_device_config_load(boot_device_config_t *config);
boot_flash_status_t boot_device_config_store(const boot_device_config_t *config);
boot_flash_status_t boot_device_config_store_preferred_address(uint8_t address);

#endif /* BOOT_DEVICE_CONFIG_H */
