#include "boot_device_config.h"

#include <stddef.h>

#include "boot_config.h"
#include "boot_crc32.h"
#include "boot_j1939.h"
#include "boot_platform.h"
#include "boot_protocol.h"

#define DEVICE_CONFIG_SLOT_COUNT \
    (BOOT_DEVICE_CONFIG_PAGE_SIZE / BOOT_DEVICE_CONFIG_RECORD_SIZE)
#define J1939_IDENTITY_MASK UINT32_C(0x001FFFFF)

_Static_assert((BOOT_DEVICE_CONFIG_PAGE_SIZE % BOOT_DEVICE_CONFIG_RECORD_SIZE) == 0U,
               "Device configuration records must fill the Flash page");
_Static_assert((BOOT_DEVICE_CONFIG_COMMIT_OFFSET % BOOT_FLASH_PROGRAM_UNIT) == 0U,
               "Device configuration commit marker must be program-unit aligned");

static bool address_valid(uint8_t address)
{
    return address < BOOT_J1939_NULL_ADDRESS;
}

static uint64_t make_default_name(void)
{
    uint8_t uid[12];
    uint32_t identity;
    uint64_t name;
    uint8_t index;

    for (index = 0U; index < 3U; ++index)
    {
        boot_protocol_write_u32_le(&uid[(uint32_t)index * 4U],
                                   boot_platform_uid_word(index));
    }
    identity = boot_crc32_compute(uid, sizeof(uid)) & J1939_IDENTITY_MASK;
    if (identity == 0U)
    {
        identity = 1U;
    }

    name = identity;
    name |= (uint64_t)BOOT_J1939_NAME_MANUFACTURER_CODE << 21U;
    name |= (uint64_t)BOOT_J1939_NAME_ECU_INSTANCE << 32U;
    name |= (uint64_t)BOOT_J1939_NAME_FUNCTION_INSTANCE << 35U;
    name |= (uint64_t)BOOT_J1939_NAME_FUNCTION << 40U;
    name |= (uint64_t)BOOT_J1939_NAME_VEHICLE_SYSTEM << 49U;
    name |= (uint64_t)BOOT_J1939_NAME_VEHICLE_SYSTEM_INSTANCE << 56U;
    name |= (uint64_t)BOOT_J1939_NAME_INDUSTRY_GROUP << 60U;
    name |= (uint64_t)BOOT_J1939_NAME_ARBITRARY_ADDRESS_CAPABLE << 63U;
    return name;
}

void boot_device_config_defaults(boot_device_config_t *config)
{
    if (config == NULL)
    {
        return;
    }

    config->name = make_default_name();
    config->generation = 0U;
    config->flags = 0U;
    config->preferred_address = BOOT_J1939_DEFAULT_PREFERRED_ADDRESS;
}

static bool raw_is_erased(const uint8_t *raw)
{
    size_t index;

    for (index = 0U; index < BOOT_DEVICE_CONFIG_RECORD_SIZE; ++index)
    {
        if (raw[index] != UINT8_MAX)
        {
            return false;
        }
    }
    return true;
}

static bool decode_record(const uint8_t *raw, boot_device_config_t *config)
{
    const uint8_t address = raw[BOOT_DEVICE_CONFIG_ADDRESS_OFFSET];
    const uint64_t name =
        boot_protocol_read_u64_le(&raw[BOOT_DEVICE_CONFIG_NAME_OFFSET]);

    if ((boot_protocol_read_u32_le(&raw[BOOT_DEVICE_CONFIG_MAGIC_OFFSET]) !=
         BOOT_DEVICE_CONFIG_MAGIC) ||
        (boot_protocol_read_u16_le(&raw[BOOT_DEVICE_CONFIG_FORMAT_OFFSET]) !=
         BOOT_DEVICE_CONFIG_FORMAT_VERSION) ||
        (boot_protocol_read_u16_le(&raw[BOOT_DEVICE_CONFIG_SIZE_OFFSET]) !=
         BOOT_DEVICE_CONFIG_RECORD_SIZE) ||
        (boot_protocol_read_u16_le(&raw[BOOT_DEVICE_CONFIG_COMMIT_OFFSET]) !=
         BOOT_DEVICE_CONFIG_COMMIT_VALUE) ||
        !address_valid(address) ||
        ((uint8_t)(address ^ raw[BOOT_DEVICE_CONFIG_ADDRESS_INV_OFFSET]) !=
         UINT8_MAX) ||
        (name == 0U) || (name == UINT64_MAX) ||
        (boot_crc32_compute(raw, BOOT_DEVICE_CONFIG_CRC_INPUT_SIZE) !=
         boot_protocol_read_u32_le(&raw[BOOT_DEVICE_CONFIG_CRC_OFFSET])))
    {
        return false;
    }

    if (config != NULL)
    {
        config->name = name;
        config->generation =
            boot_protocol_read_u32_le(&raw[BOOT_DEVICE_CONFIG_GENERATION_OFFSET]);
        config->flags =
            boot_protocol_read_u16_le(&raw[BOOT_DEVICE_CONFIG_FLAGS_OFFSET]);
        config->preferred_address = address;
    }
    return true;
}

static bool generation_is_newer(uint32_t candidate, uint32_t current)
{
    return (int32_t)(candidate - current) > 0;
}

static bool scan_records(boot_device_config_t *latest,
                         bool *have_latest,
                         uint32_t *first_erased_slot)
{
    uint8_t raw[BOOT_DEVICE_CONFIG_RECORD_SIZE];
    uint32_t slot;

    if ((latest == NULL) || (have_latest == NULL) ||
        (first_erased_slot == NULL))
    {
        return false;
    }

    *have_latest = false;
    *first_erased_slot = DEVICE_CONFIG_SLOT_COUNT;
    for (slot = 0U; slot < DEVICE_CONFIG_SLOT_COUNT; ++slot)
    {
        boot_device_config_t candidate;

        if (boot_flash_read(BOOT_FLASH_REGION_DEVICE_CONFIG,
                            slot * BOOT_DEVICE_CONFIG_RECORD_SIZE,
                            raw,
                            sizeof(raw)) != BOOT_FLASH_STATUS_OK)
        {
            return false;
        }
        if (raw_is_erased(raw))
        {
            if (*first_erased_slot == DEVICE_CONFIG_SLOT_COUNT)
            {
                *first_erased_slot = slot;
            }
            continue;
        }
        if (decode_record(raw, &candidate) &&
            (!*have_latest ||
             generation_is_newer(candidate.generation, latest->generation)))
        {
            *latest = candidate;
            *have_latest = true;
        }
    }
    return true;
}

bool boot_device_config_read(boot_device_config_t *config)
{
    boot_device_config_t latest;
    bool have_latest;
    uint32_t first_erased_slot;

    if ((config == NULL) ||
        !scan_records(&latest, &have_latest, &first_erased_slot) ||
        !have_latest)
    {
        return false;
    }

    *config = latest;
    return true;
}

void boot_device_config_load(boot_device_config_t *config)
{
    if ((config != NULL) && !boot_device_config_read(config))
    {
        boot_device_config_defaults(config);
    }
}

static void encode_record(uint8_t *raw,
                          const boot_device_config_t *config,
                          uint32_t generation)
{
    size_t index;

    for (index = 0U; index < BOOT_DEVICE_CONFIG_RECORD_SIZE; ++index)
    {
        raw[index] = UINT8_MAX;
    }
    boot_protocol_write_u32_le(&raw[BOOT_DEVICE_CONFIG_MAGIC_OFFSET],
                               BOOT_DEVICE_CONFIG_MAGIC);
    boot_protocol_write_u16_le(&raw[BOOT_DEVICE_CONFIG_FORMAT_OFFSET],
                               BOOT_DEVICE_CONFIG_FORMAT_VERSION);
    boot_protocol_write_u16_le(&raw[BOOT_DEVICE_CONFIG_SIZE_OFFSET],
                               BOOT_DEVICE_CONFIG_RECORD_SIZE);
    boot_protocol_write_u32_le(&raw[BOOT_DEVICE_CONFIG_GENERATION_OFFSET],
                               generation);
    boot_protocol_write_u64_le(&raw[BOOT_DEVICE_CONFIG_NAME_OFFSET], config->name);
    raw[BOOT_DEVICE_CONFIG_ADDRESS_OFFSET] = config->preferred_address;
    raw[BOOT_DEVICE_CONFIG_ADDRESS_INV_OFFSET] =
        (uint8_t)~config->preferred_address;
    boot_protocol_write_u16_le(&raw[BOOT_DEVICE_CONFIG_FLAGS_OFFSET],
                               config->flags);
    boot_protocol_write_u32_le(
        &raw[BOOT_DEVICE_CONFIG_CRC_OFFSET],
        boot_crc32_compute(raw, BOOT_DEVICE_CONFIG_CRC_INPUT_SIZE));
}

boot_flash_status_t boot_device_config_store(const boot_device_config_t *config)
{
    boot_device_config_t latest;
    boot_device_config_t verified;
    uint8_t raw[BOOT_DEVICE_CONFIG_RECORD_SIZE];
    uint8_t commit[BOOT_FLASH_PROGRAM_UNIT];
    bool have_latest;
    uint32_t slot;
    uint32_t generation;
    boot_flash_status_t status;

    if ((config == NULL) || !address_valid(config->preferred_address) ||
        (config->name == 0U) || (config->name == UINT64_MAX))
    {
        return BOOT_FLASH_STATUS_INVALID_ARGUMENT;
    }
    if (!scan_records(&latest, &have_latest, &slot))
    {
        return BOOT_FLASH_STATUS_HARDWARE;
    }

    generation = have_latest ? (latest.generation + 1U) : 1U;
    if (generation == 0U)
    {
        generation = 1U;
    }

    if (slot == DEVICE_CONFIG_SLOT_COUNT)
    {
        status = boot_flash_erase(BOOT_FLASH_REGION_DEVICE_CONFIG,
                                  0U,
                                  BOOT_DEVICE_CONFIG_PAGE_SIZE);
        if (status != BOOT_FLASH_STATUS_OK)
        {
            return status;
        }
        slot = 0U;
    }

    encode_record(raw, config, generation);
    status = boot_flash_write(BOOT_FLASH_REGION_DEVICE_CONFIG,
                              slot * BOOT_DEVICE_CONFIG_RECORD_SIZE,
                              raw,
                              BOOT_DEVICE_CONFIG_COMMIT_OFFSET);
    if (status != BOOT_FLASH_STATUS_OK)
    {
        return status;
    }

    boot_protocol_write_u16_le(commit, BOOT_DEVICE_CONFIG_COMMIT_VALUE);
    status = boot_flash_write(BOOT_FLASH_REGION_DEVICE_CONFIG,
                              slot * BOOT_DEVICE_CONFIG_RECORD_SIZE +
                                  BOOT_DEVICE_CONFIG_COMMIT_OFFSET,
                              commit,
                              sizeof(commit));
    if (status != BOOT_FLASH_STATUS_OK)
    {
        return status;
    }

    if (!boot_device_config_read(&verified) ||
        (verified.generation != generation) ||
        (verified.name != config->name) ||
        (verified.preferred_address != config->preferred_address) ||
        (verified.flags != config->flags))
    {
        return BOOT_FLASH_STATUS_VERIFY;
    }
    return BOOT_FLASH_STATUS_OK;
}

boot_flash_status_t boot_device_config_store_preferred_address(uint8_t address)
{
    boot_device_config_t config;

    if (!address_valid(address))
    {
        return BOOT_FLASH_STATUS_RANGE;
    }

    boot_device_config_load(&config);
    if (config.preferred_address == address)
    {
        return BOOT_FLASH_STATUS_OK;
    }
    config.preferred_address = address;
    return boot_device_config_store(&config);
}
