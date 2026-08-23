#ifndef BOOT_DEVICE_CONFIG_H
#define BOOT_DEVICE_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#include "boot_device_config_format.h"
#include "boot_flash.h"

bool boot_device_config_read(uint8_t *node_id);
uint8_t boot_device_config_load_node(void);
boot_flash_status_t boot_device_config_store_node(uint8_t node_id);

#endif /* BOOT_DEVICE_CONFIG_H */
