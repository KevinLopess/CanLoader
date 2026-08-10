#include "boot_metadata.h"

#include <stddef.h>

#include "boot_config.h"
#include "boot_crc32.h"
#include "boot_protocol.h"

#define METADATA_MAGIC_OFFSET          0U
#define METADATA_FORMAT_OFFSET         4U
#define METADATA_PROTOCOL_OFFSET       6U
#define METADATA_SIZE_OFFSET           8U
#define METADATA_IMAGE_CRC_OFFSET      12U
#define METADATA_IMAGE_VERSION_OFFSET  16U
#define METADATA_HEADER_CRC_OFFSET     20U
#define METADATA_HEADER_SIZE           24U
#define METADATA_CRC_INPUT_SIZE        20U
#define METADATA_ALIGN(value, unit)    (((value) + (unit) - 1U) / (unit) * (unit))
#define METADATA_UPDATE_MARKER_OFFSET  METADATA_ALIGN(METADATA_HEADER_SIZE, BOOT_FLASH_PROGRAM_UNIT)
#define METADATA_VALID_MARKER_OFFSET   (METADATA_UPDATE_MARKER_OFFSET + BOOT_FLASH_PROGRAM_UNIT)
#define METADATA_INVALID_MARKER_OFFSET (METADATA_VALID_MARKER_OFFSET + BOOT_FLASH_PROGRAM_UNIT)
#define METADATA_SERIALIZED_SIZE       (METADATA_INVALID_MARKER_OFFSET + BOOT_FLASH_PROGRAM_UNIT)

static void metadata_fill_erased(uint8_t *data, size_t length)
{
    size_t index;

    for (index = 0U; index < length; ++index)
    {
        data[index] = UINT8_C(0xFF);
    }
}

static void metadata_fill_marker(uint8_t *data, uint8_t value)
{
    size_t index;

    for (index = 0U; index < BOOT_FLASH_PROGRAM_UNIT; ++index)
    {
        data[index] = value;
    }
}

static bool metadata_marker_is(const uint8_t *data, uint8_t value)
{
    size_t index;

    for (index = 0U; index < BOOT_FLASH_PROGRAM_UNIT; ++index)
    {
        if (data[index] != value)
        {
            return false;
        }
    }
    return true;
}

bool boot_metadata_read(boot_metadata_t *metadata)
{
    uint8_t raw[METADATA_SERIALIZED_SIZE];
    uint32_t stored_header_crc;
    bool update_set;
    bool valid_set;
    bool invalid_set;
    bool valid_erased;
    bool invalid_erased;

    if (metadata == NULL)
    {
        return false;
    }

    if (boot_flash_read(BOOT_FLASH_REGION_METADATA, 0U, raw, sizeof(raw)) !=
        BOOT_FLASH_STATUS_OK)
    {
        return false;
    }

    if ((boot_protocol_read_u32_le(&raw[METADATA_MAGIC_OFFSET]) != BOOT_METADATA_MAGIC) ||
        (boot_protocol_read_u16_le(&raw[METADATA_FORMAT_OFFSET]) !=
         BOOT_METADATA_FORMAT_VERSION) ||
        (boot_protocol_read_u16_le(&raw[METADATA_PROTOCOL_OFFSET]) !=
         BOOT_PROTOCOL_VERSION))
    {
        return false;
    }

    stored_header_crc = boot_protocol_read_u32_le(&raw[METADATA_HEADER_CRC_OFFSET]);
    if (boot_crc32_compute(raw, METADATA_CRC_INPUT_SIZE) != stored_header_crc)
    {
        return false;
    }

    metadata->image_size = boot_protocol_read_u32_le(&raw[METADATA_SIZE_OFFSET]);
    metadata->image_crc32 = boot_protocol_read_u32_le(&raw[METADATA_IMAGE_CRC_OFFSET]);
    metadata->image_version = boot_protocol_read_u32_le(&raw[METADATA_IMAGE_VERSION_OFFSET]);

    update_set = metadata_marker_is(&raw[METADATA_UPDATE_MARKER_OFFSET], 0U);
    valid_set = metadata_marker_is(&raw[METADATA_VALID_MARKER_OFFSET], 0U);
    invalid_set = metadata_marker_is(&raw[METADATA_INVALID_MARKER_OFFSET], 0U);
    valid_erased = metadata_marker_is(&raw[METADATA_VALID_MARKER_OFFSET], UINT8_MAX);
    invalid_erased = metadata_marker_is(&raw[METADATA_INVALID_MARKER_OFFSET], UINT8_MAX);

    if (!update_set)
    {
        return false;
    }
    if (invalid_set)
    {
        metadata->state = BOOT_METADATA_STATE_INVALID;
    }
    else if (valid_set && invalid_erased)
    {
        metadata->state = BOOT_METADATA_STATE_VALID;
    }
    else if (valid_erased && invalid_erased)
    {
        metadata->state = BOOT_METADATA_STATE_UPDATING;
    }
    else
    {
        return false;
    }

    if ((metadata->image_size < 8U) ||
        (metadata->image_size > BOOT_APPLICATION_MAX_SIZE))
    {
        return false;
    }

    return true;
}

boot_flash_status_t boot_metadata_begin(uint32_t image_size, uint32_t image_crc32)
{
    uint8_t raw[METADATA_SERIALIZED_SIZE];
    uint32_t header_crc;
    boot_flash_status_t status;

    if ((image_size < 8U) || (image_size > BOOT_APPLICATION_MAX_SIZE))
    {
        return BOOT_FLASH_STATUS_RANGE;
    }

    status = boot_flash_erase(BOOT_FLASH_REGION_METADATA, 0U, BOOT_METADATA_PAGE_SIZE);
    if (status != BOOT_FLASH_STATUS_OK)
    {
        return status;
    }

    metadata_fill_erased(raw, sizeof(raw));
    boot_protocol_write_u32_le(&raw[METADATA_MAGIC_OFFSET], BOOT_METADATA_MAGIC);
    boot_protocol_write_u16_le(&raw[METADATA_FORMAT_OFFSET],
                               BOOT_METADATA_FORMAT_VERSION);
    boot_protocol_write_u16_le(&raw[METADATA_PROTOCOL_OFFSET], BOOT_PROTOCOL_VERSION);
    boot_protocol_write_u32_le(&raw[METADATA_SIZE_OFFSET], image_size);
    boot_protocol_write_u32_le(&raw[METADATA_IMAGE_CRC_OFFSET], image_crc32);
    boot_protocol_write_u32_le(&raw[METADATA_IMAGE_VERSION_OFFSET], 0U);
    header_crc = boot_crc32_compute(raw, METADATA_CRC_INPUT_SIZE);
    boot_protocol_write_u32_le(&raw[METADATA_HEADER_CRC_OFFSET], header_crc);
    metadata_fill_marker(&raw[METADATA_UPDATE_MARKER_OFFSET], 0U);

    status = boot_flash_write(BOOT_FLASH_REGION_METADATA, 0U, raw, sizeof(raw));
    if (status != BOOT_FLASH_STATUS_OK)
    {
        return status;
    }

    {
        boot_metadata_t metadata;

        if (!boot_metadata_read(&metadata) ||
            (metadata.state != BOOT_METADATA_STATE_UPDATING) ||
            (metadata.image_size != image_size) ||
            (metadata.image_crc32 != image_crc32))
        {
            return BOOT_FLASH_STATUS_VERIFY;
        }
    }

    return BOOT_FLASH_STATUS_OK;
}

static boot_flash_status_t metadata_program_marker(uint32_t marker_offset)
{
    uint8_t marker[BOOT_FLASH_PROGRAM_UNIT];

    metadata_fill_marker(marker, 0U);
    return boot_flash_write(BOOT_FLASH_REGION_METADATA,
                            marker_offset,
                            marker,
                            sizeof(marker));
}

boot_flash_status_t boot_metadata_mark_valid(void)
{
    boot_metadata_t metadata;
    boot_flash_status_t status;

    if (!boot_metadata_read(&metadata) ||
        (metadata.state != BOOT_METADATA_STATE_UPDATING))
    {
        return BOOT_FLASH_STATUS_VERIFY;
    }

    status = metadata_program_marker(METADATA_VALID_MARKER_OFFSET);
    if ((status != BOOT_FLASH_STATUS_OK) || !boot_metadata_read(&metadata) ||
        (metadata.state != BOOT_METADATA_STATE_VALID))
    {
        return (status == BOOT_FLASH_STATUS_OK) ? BOOT_FLASH_STATUS_VERIFY : status;
    }
    return BOOT_FLASH_STATUS_OK;
}

boot_flash_status_t boot_metadata_mark_invalid(void)
{
    boot_metadata_t metadata;
    boot_flash_status_t status;

    if (!boot_metadata_read(&metadata))
    {
        return BOOT_FLASH_STATUS_OK;
    }

    if (metadata.state == BOOT_METADATA_STATE_INVALID)
    {
        return BOOT_FLASH_STATUS_OK;
    }

    status = metadata_program_marker(METADATA_INVALID_MARKER_OFFSET);
    if ((status != BOOT_FLASH_STATUS_OK) || !boot_metadata_read(&metadata) ||
        (metadata.state != BOOT_METADATA_STATE_INVALID))
    {
        return (status == BOOT_FLASH_STATUS_OK) ? BOOT_FLASH_STATUS_VERIFY : status;
    }
    return BOOT_FLASH_STATUS_OK;
}
