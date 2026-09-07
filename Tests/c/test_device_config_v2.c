#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "boot_config.h"
#include "boot_device_config.h"
#include "boot_flash.h"

static uint8_t config_page[BOOT_DEVICE_CONFIG_PAGE_SIZE];
static size_t erase_calls;
static size_t write_calls;
static size_t fail_write_call;

static void fake_reset(void)
{
    memset(config_page, 0xFF, sizeof(config_page));
    erase_calls = 0U;
    write_calls = 0U;
    fail_write_call = 0U;
}

uint32_t boot_platform_uid_word(uint8_t index)
{
    static const uint32_t uid[3] = {
        UINT32_C(0x11223344),
        UINT32_C(0x55667788),
        UINT32_C(0x99AABBCC)
    };

    return (index < 3U) ? uid[index] : 0U;
}

boot_flash_status_t boot_flash_read(boot_flash_region_t region,
                                    uint32_t offset,
                                    uint8_t *data,
                                    size_t length)
{
    if ((region != BOOT_FLASH_REGION_DEVICE_CONFIG) ||
        (data == NULL) || (offset > sizeof(config_page)) ||
        (length > (sizeof(config_page) - offset)))
    {
        return BOOT_FLASH_STATUS_RANGE;
    }
    memcpy(data, &config_page[offset], length);
    return BOOT_FLASH_STATUS_OK;
}

boot_flash_status_t boot_flash_erase(boot_flash_region_t region,
                                     uint32_t offset,
                                     size_t length)
{
    if ((region != BOOT_FLASH_REGION_DEVICE_CONFIG) || (offset != 0U) ||
        (length != sizeof(config_page)))
    {
        return BOOT_FLASH_STATUS_RANGE;
    }
    ++erase_calls;
    memset(config_page, 0xFF, sizeof(config_page));
    return BOOT_FLASH_STATUS_OK;
}

boot_flash_status_t boot_flash_write(boot_flash_region_t region,
                                     uint32_t offset,
                                     const uint8_t *data,
                                     size_t length)
{
    size_t index;

    ++write_calls;
    if ((fail_write_call != 0U) && (write_calls == fail_write_call))
    {
        return BOOT_FLASH_STATUS_PROGRAM;
    }
    if ((region != BOOT_FLASH_REGION_DEVICE_CONFIG) || (data == NULL) ||
        ((offset % BOOT_FLASH_PROGRAM_UNIT) != 0U) ||
        ((length % BOOT_FLASH_PROGRAM_UNIT) != 0U) ||
        (offset > sizeof(config_page)) ||
        (length > (sizeof(config_page) - offset)))
    {
        return BOOT_FLASH_STATUS_RANGE;
    }
    for (index = 0U; index < length; ++index)
    {
        if ((uint8_t)(config_page[offset + index] & data[index]) != data[index])
        {
            return BOOT_FLASH_STATUS_PROGRAM;
        }
    }
    for (index = 0U; index < length; ++index)
    {
        config_page[offset + index] &= data[index];
    }
    return BOOT_FLASH_STATUS_OK;
}

static void test_defaults_are_stable_and_j1939_valid(void)
{
    boot_device_config_t first;
    boot_device_config_t second;

    fake_reset();
    boot_device_config_defaults(&first);
    boot_device_config_defaults(&second);
    assert(first.name == second.name);
    assert(first.name != 0U);
    assert(first.name != UINT64_MAX);
    assert((first.name >> 63U) == BOOT_J1939_NAME_ARBITRARY_ADDRESS_CAPABLE);
    assert(first.preferred_address == BOOT_J1939_DEFAULT_PREFERRED_ADDRESS);
    assert(first.generation == 0U);
    assert(first.flags == 0U);
    assert(!boot_device_config_read(&second));
    boot_device_config_load(&second);
    assert(second.name == first.name);
}

static void test_append_and_select_latest_valid_record(void)
{
    boot_device_config_t config;
    boot_device_config_t loaded;

    fake_reset();
    boot_device_config_defaults(&config);
    assert(boot_device_config_store(&config) == BOOT_FLASH_STATUS_OK);
    assert(erase_calls == 0U);
    assert(write_calls == 2U);
    assert(boot_device_config_read(&loaded));
    assert(loaded.generation == 1U);
    assert(loaded.name == config.name);

    assert(boot_device_config_store_preferred_address(UINT8_C(0x82)) ==
           BOOT_FLASH_STATUS_OK);
    assert(erase_calls == 0U);
    assert(write_calls == 4U);
    assert(boot_device_config_read(&loaded));
    assert(loaded.generation == 2U);
    assert(loaded.preferred_address == UINT8_C(0x82));

    /* Corrupt the newest CRC: the previous committed generation still wins. */
    config_page[BOOT_DEVICE_CONFIG_RECORD_SIZE +
                BOOT_DEVICE_CONFIG_CRC_OFFSET] ^= UINT8_C(1);
    assert(boot_device_config_read(&loaded));
    assert(loaded.generation == 1U);
    assert(loaded.preferred_address == BOOT_J1939_DEFAULT_PREFERRED_ADDRESS);
}

static void test_torn_record_is_never_selected_or_reused(void)
{
    boot_device_config_t config;
    boot_device_config_t loaded;

    fake_reset();
    boot_device_config_defaults(&config);
    assert(boot_device_config_store(&config) == BOOT_FLASH_STATUS_OK);

    config.preferred_address = UINT8_C(0x81);
    fail_write_call = write_calls + 2U; /* Fail the program unit with commit. */
    assert(boot_device_config_store(&config) == BOOT_FLASH_STATUS_PROGRAM);
    assert(boot_device_config_read(&loaded));
    assert(loaded.generation == 1U);

    fail_write_call = 0U;
    config.preferred_address = UINT8_C(0x82);
    assert(boot_device_config_store(&config) == BOOT_FLASH_STATUS_OK);
    assert(boot_device_config_read(&loaded));
    assert(loaded.generation == 2U);
    assert(loaded.preferred_address == UINT8_C(0x82));
    assert(config_page[BOOT_DEVICE_CONFIG_RECORD_SIZE +
                       BOOT_DEVICE_CONFIG_COMMIT_OFFSET] == UINT8_C(0xFF));
    assert(config_page[(2U * BOOT_DEVICE_CONFIG_RECORD_SIZE) +
                       BOOT_DEVICE_CONFIG_COMMIT_OFFSET] == UINT8_C(0x00));
}

static void test_full_journal_compacts(void)
{
    boot_device_config_t config;
    boot_device_config_t loaded;
    size_t index;
    const size_t slots =
        BOOT_DEVICE_CONFIG_PAGE_SIZE / BOOT_DEVICE_CONFIG_RECORD_SIZE;

    fake_reset();
    boot_device_config_defaults(&config);
    for (index = 0U; index < slots; ++index)
    {
        config.preferred_address = (uint8_t)(UINT8_C(0x80) + (index % 8U));
        assert(boot_device_config_store(&config) == BOOT_FLASH_STATUS_OK);
    }
    assert(erase_calls == 0U);

    config.preferred_address = UINT8_C(0x90);
    assert(boot_device_config_store(&config) == BOOT_FLASH_STATUS_OK);
    assert(erase_calls == 1U);
    assert(boot_device_config_read(&loaded));
    assert(loaded.generation == (uint32_t)slots + 1U);
    assert(loaded.preferred_address == UINT8_C(0x90));
}

static void test_rejects_invalid_configuration(void)
{
    boot_device_config_t config;

    fake_reset();
    boot_device_config_defaults(&config);
    config.preferred_address = BOOT_J1939_NULL_ADDRESS;
    assert(boot_device_config_store(&config) ==
           BOOT_FLASH_STATUS_INVALID_ARGUMENT);
    assert(boot_device_config_store_preferred_address(
               BOOT_J1939_GLOBAL_ADDRESS) == BOOT_FLASH_STATUS_RANGE);
    config.preferred_address = UINT8_C(0x80);
    config.name = 0U;
    assert(boot_device_config_store(&config) ==
           BOOT_FLASH_STATUS_INVALID_ARGUMENT);
}

int main(void)
{
    test_defaults_are_stable_and_j1939_valid();
    test_append_and_select_latest_valid_record();
    test_torn_record_is_never_selected_or_reused();
    test_full_journal_compacts();
    test_rejects_invalid_configuration();

    puts("test_device_config_v2: OK");
    return 0;
}
