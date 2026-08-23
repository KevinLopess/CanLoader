#include "boot_device_config.h"

#include <stddef.h>

#include "boot_config.h"
#include "boot_crc32.h"
#include "boot_protocol.h"

static bool node_id_valid(uint8_t node_id)
{
    return (node_id >= BOOT_MIN_NODE_ID) && (node_id <= BOOT_MAX_NODE_ID);
}

bool boot_device_config_read(uint8_t *node_id)
{
    uint8_t raw[BOOT_DEVICE_CONFIG_SERIALIZED_SIZE];
    uint32_t stored_crc;
    uint8_t stored_node;

    if (node_id == NULL)
    {
        return false;
    }
    if (boot_flash_read(BOOT_FLASH_REGION_DEVICE_CONFIG,
                        0U,
                        raw,
                        sizeof(raw)) != BOOT_FLASH_STATUS_OK)
    {
        return false;
    }
    if ((boot_protocol_read_u32_le(&raw[BOOT_DEVICE_CONFIG_MAGIC_OFFSET]) !=
         BOOT_DEVICE_CONFIG_MAGIC) ||
        (boot_protocol_read_u16_le(&raw[BOOT_DEVICE_CONFIG_FORMAT_OFFSET]) !=
         BOOT_DEVICE_CONFIG_FORMAT_VERSION))
    {
        return false;
    }

    stored_node = raw[BOOT_DEVICE_CONFIG_NODE_OFFSET];
    if (!node_id_valid(stored_node) ||
        ((uint8_t)(stored_node ^ raw[BOOT_DEVICE_CONFIG_NODE_INV_OFFSET]) != UINT8_MAX))
    {
        return false;
    }

    stored_crc = boot_protocol_read_u32_le(&raw[BOOT_DEVICE_CONFIG_CRC_OFFSET]);
    if (boot_crc32_compute(raw, BOOT_DEVICE_CONFIG_CRC_INPUT_SIZE) != stored_crc)
    {
        return false;
    }

    *node_id = stored_node;
    return true;
}

uint8_t boot_device_config_load_node(void)
{
    uint8_t node_id;

    return boot_device_config_read(&node_id) ? node_id : BOOT_DEFAULT_NODE_ID;
}

boot_flash_status_t boot_device_config_store_node(uint8_t node_id)
{
    uint8_t raw[BOOT_DEVICE_CONFIG_SERIALIZED_SIZE] = {0};
    uint32_t crc;
    boot_flash_status_t status;
    uint8_t stored_node;

    if (!node_id_valid(node_id))
    {
        return BOOT_FLASH_STATUS_RANGE;
    }

    boot_protocol_write_u32_le(&raw[BOOT_DEVICE_CONFIG_MAGIC_OFFSET],
                               BOOT_DEVICE_CONFIG_MAGIC);
    boot_protocol_write_u16_le(&raw[BOOT_DEVICE_CONFIG_FORMAT_OFFSET],
                               BOOT_DEVICE_CONFIG_FORMAT_VERSION);
    raw[BOOT_DEVICE_CONFIG_NODE_OFFSET] = node_id;
    raw[BOOT_DEVICE_CONFIG_NODE_INV_OFFSET] = (uint8_t)~node_id;
    crc = boot_crc32_compute(raw, BOOT_DEVICE_CONFIG_CRC_INPUT_SIZE);
    boot_protocol_write_u32_le(&raw[BOOT_DEVICE_CONFIG_CRC_OFFSET], crc);

    status = boot_flash_erase(BOOT_FLASH_REGION_DEVICE_CONFIG,
                              0U,
                              BOOT_DEVICE_CONFIG_PAGE_SIZE);
    if (status != BOOT_FLASH_STATUS_OK)
    {
        return status;
    }
    status = boot_flash_write(BOOT_FLASH_REGION_DEVICE_CONFIG,
                              0U,
                              raw,
                              sizeof(raw));
    if (status != BOOT_FLASH_STATUS_OK)
    {
        return status;
    }
    if (!boot_device_config_read(&stored_node) || (stored_node != node_id))
    {
        return BOOT_FLASH_STATUS_VERIFY;
    }

    return BOOT_FLASH_STATUS_OK;
}
