#include "boot_image.h"

#include <stddef.h>

#include "boot_config.h"
#include "boot_crc32.h"
#include "boot_flash.h"
#include "boot_platform.h"
#include "boot_protocol.h"

#define IMAGE_CRC_BUFFER_SIZE 128U

bool boot_image_calculate_crc(uint32_t image_size, uint32_t *crc32)
{
    uint8_t buffer[IMAGE_CRC_BUFFER_SIZE];
    uint32_t offset = 0U;
    uint32_t crc = BOOT_CRC32_INITIAL_VALUE;

    if ((crc32 == NULL) || (image_size < 8U) ||
        (image_size > BOOT_APPLICATION_MAX_SIZE))
    {
        return false;
    }

    while (offset < image_size)
    {
        const uint32_t remaining = image_size - offset;
        const size_t chunk = (remaining > sizeof(buffer)) ? sizeof(buffer) : remaining;

        if (boot_flash_read(BOOT_FLASH_REGION_APPLICATION, offset, buffer, chunk) !=
            BOOT_FLASH_STATUS_OK)
        {
            return false;
        }
        crc = boot_crc32_update(crc, buffer, chunk);
        offset += (uint32_t)chunk;
    }

    *crc32 = boot_crc32_finalize(crc);
    return true;
}

bool boot_image_vectors_valid(uint32_t image_size,
                              uint32_t *initial_stack_pointer,
                              uint32_t *reset_handler)
{
    uint8_t vectors[8];
    uint32_t stack_pointer;
    uint32_t handler;
    uint32_t handler_address;
    uint32_t image_end;

    if ((image_size < sizeof(vectors)) || (image_size > BOOT_APPLICATION_MAX_SIZE) ||
        (initial_stack_pointer == NULL) || (reset_handler == NULL))
    {
        return false;
    }

    if (boot_flash_read(BOOT_FLASH_REGION_APPLICATION, 0U, vectors, sizeof(vectors)) !=
        BOOT_FLASH_STATUS_OK)
    {
        return false;
    }

    stack_pointer = boot_protocol_read_u32_le(&vectors[0]);
    handler = boot_protocol_read_u32_le(&vectors[4]);
    handler_address = handler & ~UINT32_C(1);
    image_end = BOOT_APPLICATION_ADDRESS + image_size;

    if ((stack_pointer <= BOOT_SRAM_START_ADDRESS) ||
        (stack_pointer > BOOT_SRAM_END_ADDRESS) ||
        ((stack_pointer & UINT32_C(0x7)) != 0U) ||
        ((handler & UINT32_C(1)) == 0U) ||
        (handler_address < BOOT_APPLICATION_ADDRESS) ||
        (handler_address >= image_end))
    {
        return false;
    }

    *initial_stack_pointer = stack_pointer;
    *reset_handler = handler;
    return true;
}

bool boot_image_validate_candidate(uint32_t image_size, uint32_t expected_crc32)
{
    uint32_t computed_crc;
    uint32_t stack_pointer;
    uint32_t reset_handler;

    if (!boot_image_vectors_valid(image_size, &stack_pointer, &reset_handler))
    {
        return false;
    }

    if (!boot_image_calculate_crc(image_size, &computed_crc))
    {
        return false;
    }

    return computed_crc == expected_crc32;
}

bool boot_image_validate(boot_metadata_t *metadata)
{
    boot_metadata_t loaded;

    if (!boot_metadata_read(&loaded) ||
        (loaded.state != BOOT_METADATA_STATE_VALID) ||
        !boot_image_validate_candidate(loaded.image_size, loaded.image_crc32))
    {
        return false;
    }

    if (metadata != NULL)
    {
        *metadata = loaded;
    }
    return true;
}

void boot_image_jump(void)
{
    boot_metadata_t metadata;
    uint32_t stack_pointer;
    uint32_t reset_handler;

    if (!boot_image_validate(&metadata) ||
        !boot_image_vectors_valid(metadata.image_size, &stack_pointer, &reset_handler))
    {
        boot_platform_reset();
    }

    boot_platform_prepare_handoff();
    boot_platform_jump(BOOT_APPLICATION_ADDRESS, stack_pointer, reset_handler);
}
