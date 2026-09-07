#include "boot_flash.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include "boot_config.h"
#include "stm32g0xx_hal.h"

_Static_assert(FLASH_PAGE_SIZE == BOOT_FLASH_PAGE_SIZE,
               "STM32G0 HAL page size does not match target configuration");
_Static_assert(BOOT_FLASH_PROGRAM_UNIT == 8U,
               "STM32G0 main Flash must be programmed as doublewords");

static bool flash_ready;

static bool region_parameters(boot_flash_region_t region,
                              uint32_t *base_address,
                              uint32_t *region_size)
{
    if ((base_address == NULL) || (region_size == NULL))
    {
        return false;
    }

    switch (region)
    {
        case BOOT_FLASH_REGION_APPLICATION:
            *base_address = BOOT_APPLICATION_ADDRESS;
            *region_size = BOOT_APPLICATION_MAX_SIZE;
            return true;
        case BOOT_FLASH_REGION_METADATA:
            *base_address = BOOT_METADATA_ADDRESS;
            *region_size = BOOT_METADATA_PAGE_SIZE;
            return true;
        case BOOT_FLASH_REGION_DEVICE_CONFIG:
            *base_address = BOOT_DEVICE_CONFIG_ADDRESS;
            *region_size = BOOT_DEVICE_CONFIG_PAGE_SIZE;
            return true;
        default:
            return false;
    }
}

boot_flash_status_t boot_flash_init(void)
{
    const uint16_t detected_kib =
        *(const volatile uint16_t *)(uintptr_t)FLASHSIZE_BASE;
    const bool default_bank_mapping =
        (FLASH->OPTR & FLASH_OPTR_nSWAP_BANK) != 0U;

    flash_ready =
        (detected_kib >= (BOOT_FLASH_TOTAL_SIZE / 1024U)) &&
        default_bank_mapping;
    return flash_ready ? BOOT_FLASH_STATUS_OK : BOOT_FLASH_STATUS_HARDWARE;
}

bool boot_flash_is_valid_range(boot_flash_region_t region,
                               uint32_t offset,
                               size_t length)
{
    uint32_t base_address;
    uint32_t region_size;

    if (!region_parameters(region, &base_address, &region_size))
    {
        return false;
    }
    (void)base_address;
#if SIZE_MAX > UINT32_MAX
    if (length > UINT32_MAX)
    {
        return false;
    }
#endif
    return (offset <= region_size) &&
           ((uint32_t)length <= (region_size - offset));
}

static boot_flash_status_t erase_pages(uint32_t address, uint32_t page_count)
{
    boot_flash_status_t result = BOOT_FLASH_STATUS_OK;

    while (page_count > 0U)
    {
        FLASH_EraseInitTypeDef erase = {0};
        const uint32_t bank_size = FLASH_BANK_SIZE;
        const bool second_bank = (FLASH_BANK_NB == 2U) &&
                                 (address >= (FLASH_BASE + bank_size));
        const uint32_t bank_base = second_bank ?
                                   (FLASH_BASE + bank_size) : FLASH_BASE;
        const uint32_t pages_available =
            ((bank_base + bank_size) - address) / FLASH_PAGE_SIZE;
        const uint32_t pages_now = (page_count < pages_available) ?
                                   page_count : pages_available;
        uint32_t page_error = UINT32_MAX;

        if (pages_now == 0U)
        {
            return BOOT_FLASH_STATUS_RANGE;
        }
        erase.TypeErase = FLASH_TYPEERASE_PAGES;
        erase.Banks = second_bank ? FLASH_BANK_2 : FLASH_BANK_1;
        erase.Page = (address - bank_base) / FLASH_PAGE_SIZE;
        erase.NbPages = pages_now;
        if ((HAL_FLASHEx_Erase(&erase, &page_error) != HAL_OK) ||
            (page_error != UINT32_MAX))
        {
            result = BOOT_FLASH_STATUS_ERASE;
            break;
        }
        address += pages_now * FLASH_PAGE_SIZE;
        page_count -= pages_now;
    }
    return result;
}

boot_flash_status_t boot_flash_erase(boot_flash_region_t region,
                                     uint32_t offset,
                                     size_t length)
{
    uint32_t base_address;
    uint32_t region_size;
    uint32_t address;
    boot_flash_status_t result;

    if (!flash_ready)
    {
        return BOOT_FLASH_STATUS_HARDWARE;
    }
    if (!region_parameters(region, &base_address, &region_size) ||
        !boot_flash_is_valid_range(region, offset, length) || (length == 0U))
    {
        return BOOT_FLASH_STATUS_RANGE;
    }
    if (((offset % BOOT_FLASH_PAGE_SIZE) != 0U) ||
        ((length % BOOT_FLASH_PAGE_SIZE) != 0U))
    {
        return BOOT_FLASH_STATUS_ALIGNMENT;
    }

    if (HAL_FLASH_Unlock() != HAL_OK)
    {
        return BOOT_FLASH_STATUS_HARDWARE;
    }
    result = erase_pages(base_address + offset,
                         (uint32_t)length / BOOT_FLASH_PAGE_SIZE);
    (void)HAL_FLASH_Lock();
    if (result != BOOT_FLASH_STATUS_OK)
    {
        return result;
    }

    for (address = base_address + offset;
         address < (base_address + offset + (uint32_t)length);
         address += BOOT_FLASH_PROGRAM_UNIT)
    {
        if (*(const volatile uint64_t *)(uintptr_t)address != UINT64_MAX)
        {
            return BOOT_FLASH_STATUS_VERIFY;
        }
    }
    return BOOT_FLASH_STATUS_OK;
}

static uint64_t read_u64_le(const uint8_t *data)
{
    uint64_t value = 0U;
    uint8_t index;

    for (index = 0U; index < 8U; ++index)
    {
        value |= (uint64_t)data[index] << ((uint32_t)index * 8U);
    }
    return value;
}

boot_flash_status_t boot_flash_write(boot_flash_region_t region,
                                     uint32_t offset,
                                     const uint8_t *data,
                                     size_t length)
{
    uint32_t base_address;
    uint32_t region_size;
    size_t index;
    boot_flash_status_t result = BOOT_FLASH_STATUS_OK;

    if (!flash_ready)
    {
        return BOOT_FLASH_STATUS_HARDWARE;
    }
    if ((data == NULL) || (length == 0U))
    {
        return BOOT_FLASH_STATUS_INVALID_ARGUMENT;
    }
    if (!region_parameters(region, &base_address, &region_size) ||
        !boot_flash_is_valid_range(region, offset, length))
    {
        return BOOT_FLASH_STATUS_RANGE;
    }
    if (((offset % BOOT_FLASH_PROGRAM_UNIT) != 0U) ||
        ((length % BOOT_FLASH_PROGRAM_UNIT) != 0U))
    {
        return BOOT_FLASH_STATUS_ALIGNMENT;
    }
    if (HAL_FLASH_Unlock() != HAL_OK)
    {
        return BOOT_FLASH_STATUS_HARDWARE;
    }

    for (index = 0U; index < length; index += BOOT_FLASH_PROGRAM_UNIT)
    {
        const uint32_t address = base_address + offset + (uint32_t)index;
        const uint64_t desired = read_u64_le(&data[index]);
        const uint64_t existing =
            *(const volatile uint64_t *)(uintptr_t)address;

        if (existing == desired)
        {
            continue;
        }
        if (existing != UINT64_MAX)
        {
            result = BOOT_FLASH_STATUS_PROGRAM;
            break;
        }
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD,
                              address,
                              desired) != HAL_OK)
        {
            result = BOOT_FLASH_STATUS_PROGRAM;
            break;
        }
        if (*(const volatile uint64_t *)(uintptr_t)address != desired)
        {
            result = BOOT_FLASH_STATUS_VERIFY;
            break;
        }
    }
    (void)HAL_FLASH_Lock();
    return result;
}

boot_flash_status_t boot_flash_read(boot_flash_region_t region,
                                    uint32_t offset,
                                    uint8_t *data,
                                    size_t length)
{
    uint32_t base_address;
    uint32_t region_size;
    const volatile uint8_t *source;
    size_t index;

    if ((data == NULL) && (length != 0U))
    {
        return BOOT_FLASH_STATUS_INVALID_ARGUMENT;
    }
    if (!region_parameters(region, &base_address, &region_size) ||
        !boot_flash_is_valid_range(region, offset, length))
    {
        return BOOT_FLASH_STATUS_RANGE;
    }

    source = (const volatile uint8_t *)(uintptr_t)(base_address + offset);
    for (index = 0U; index < length; ++index)
    {
        data[index] = source[index];
    }
    return BOOT_FLASH_STATUS_OK;
}
