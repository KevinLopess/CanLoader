#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boot_config.h"
#include "boot_crc32.h"
#include "boot_device_config.h"
#include "boot_flash.h"
#include "boot_image.h"
#include "boot_metadata.h"
#include "boot_platform.h"
#include "boot_protocol.h"

#define TEST_METADATA_HEADER_SIZE            24U
#define TEST_METADATA_CRC_INPUT_SIZE         20U
#define TEST_METADATA_HEADER_CRC_OFFSET      20U
#define TEST_METADATA_UPDATE_MARKER_OFFSET   24U
#define TEST_METADATA_VALID_MARKER_OFFSET    (TEST_METADATA_UPDATE_MARKER_OFFSET + \
                                              BOOT_FLASH_PROGRAM_UNIT)
#define TEST_METADATA_INVALID_MARKER_OFFSET  (TEST_METADATA_VALID_MARKER_OFFSET + \
                                              BOOT_FLASH_PROGRAM_UNIT)
#define TEST_METADATA_SERIALIZED_SIZE        (TEST_METADATA_INVALID_MARKER_OFFSET + \
                                              BOOT_FLASH_PROGRAM_UNIT)
#define FAKE_ACCESS_LOG_CAPACITY              64U

_Static_assert(BOOT_FLASH_PROGRAM_UNIT == 2U,
               "marker tear tests expect the STM32F103 half-word program unit");
_Static_assert(TEST_METADATA_SERIALIZED_SIZE <= BOOT_METADATA_PAGE_SIZE,
               "serialized metadata must fit its Flash page");

typedef struct
{
    boot_flash_region_t region;
    uint32_t offset;
    size_t length;
} fake_flash_access_t;

typedef struct
{
    boot_flash_status_t read_status;
    boot_flash_status_t erase_status;
    boot_flash_status_t write_status;
    fake_flash_access_t reads[FAKE_ACCESS_LOG_CAPACITY];
    fake_flash_access_t erases[FAKE_ACCESS_LOG_CAPACITY];
    fake_flash_access_t writes[FAKE_ACCESS_LOG_CAPACITY];
    size_t read_calls;
    size_t erase_calls;
    size_t write_calls;
} fake_flash_t;

static uint8_t application_flash[BOOT_APPLICATION_MAX_SIZE];
static uint8_t metadata_flash[BOOT_METADATA_PAGE_SIZE];
static uint8_t device_config_flash[BOOT_DEVICE_CONFIG_PAGE_SIZE];
static fake_flash_t fake_flash;

static uint8_t *fake_region_storage(boot_flash_region_t region, size_t *region_size)
{
    assert(region_size != NULL);

    switch (region)
    {
        case BOOT_FLASH_REGION_APPLICATION:
            *region_size = sizeof(application_flash);
            return application_flash;

        case BOOT_FLASH_REGION_METADATA:
            *region_size = sizeof(metadata_flash);
            return metadata_flash;

        case BOOT_FLASH_REGION_DEVICE_CONFIG:
            *region_size = sizeof(device_config_flash);
            return device_config_flash;

        default:
            *region_size = 0U;
            return NULL;
    }
}

static void fake_flash_reset(void)
{
    memset(application_flash, UINT8_C(0xFF), sizeof(application_flash));
    memset(metadata_flash, UINT8_C(0xFF), sizeof(metadata_flash));
    memset(device_config_flash, UINT8_C(0xFF), sizeof(device_config_flash));
    fake_flash = (fake_flash_t){
        .read_status = BOOT_FLASH_STATUS_OK,
        .erase_status = BOOT_FLASH_STATUS_OK,
        .write_status = BOOT_FLASH_STATUS_OK
    };
}

static void fake_flash_clear_log(void)
{
    fake_flash.read_calls = 0U;
    fake_flash.erase_calls = 0U;
    fake_flash.write_calls = 0U;
}

static void fake_log_access(fake_flash_access_t *log,
                            size_t *count,
                            boot_flash_region_t region,
                            uint32_t offset,
                            size_t length)
{
    assert(log != NULL);
    assert(count != NULL);
    assert(*count < FAKE_ACCESS_LOG_CAPACITY);
    log[*count] = (fake_flash_access_t){
        .region = region,
        .offset = offset,
        .length = length
    };
    ++(*count);
}

boot_flash_status_t boot_flash_init(void)
{
    return BOOT_FLASH_STATUS_OK;
}

bool boot_flash_is_valid_range(boot_flash_region_t region,
                               uint32_t offset,
                               size_t length)
{
    size_t region_size;
    const uint8_t *storage = fake_region_storage(region, &region_size);

    return (storage != NULL) && ((size_t)offset <= region_size) &&
           (length <= (region_size - (size_t)offset));
}

boot_flash_status_t boot_flash_erase(boot_flash_region_t region,
                                     uint32_t offset,
                                     size_t length)
{
    size_t region_size;
    uint8_t *storage;

    fake_log_access(fake_flash.erases,
                    &fake_flash.erase_calls,
                    region,
                    offset,
                    length);
    if (fake_flash.erase_status != BOOT_FLASH_STATUS_OK)
    {
        return fake_flash.erase_status;
    }
    if (!boot_flash_is_valid_range(region, offset, length))
    {
        return BOOT_FLASH_STATUS_RANGE;
    }

    storage = fake_region_storage(region, &region_size);
    (void)region_size;
    memset(&storage[offset], UINT8_C(0xFF), length);
    return BOOT_FLASH_STATUS_OK;
}

boot_flash_status_t boot_flash_write(boot_flash_region_t region,
                                     uint32_t offset,
                                     const uint8_t *data,
                                     size_t length)
{
    size_t index;
    size_t region_size;
    uint8_t *storage;

    fake_log_access(fake_flash.writes,
                    &fake_flash.write_calls,
                    region,
                    offset,
                    length);
    if (fake_flash.write_status != BOOT_FLASH_STATUS_OK)
    {
        return fake_flash.write_status;
    }
    if ((data == NULL) || !boot_flash_is_valid_range(region, offset, length))
    {
        return (data == NULL) ? BOOT_FLASH_STATUS_INVALID_ARGUMENT :
                                BOOT_FLASH_STATUS_RANGE;
    }

    storage = fake_region_storage(region, &region_size);
    (void)region_size;
    for (index = 0U; index < length; ++index)
    {
        storage[offset + index] &= data[index];
    }
    return BOOT_FLASH_STATUS_OK;
}

boot_flash_status_t boot_flash_read(boot_flash_region_t region,
                                    uint32_t offset,
                                    uint8_t *data,
                                    size_t length)
{
    size_t region_size;
    uint8_t *storage;

    fake_log_access(fake_flash.reads,
                    &fake_flash.read_calls,
                    region,
                    offset,
                    length);
    if (fake_flash.read_status != BOOT_FLASH_STATUS_OK)
    {
        return fake_flash.read_status;
    }
    if ((data == NULL) || !boot_flash_is_valid_range(region, offset, length))
    {
        return (data == NULL) ? BOOT_FLASH_STATUS_INVALID_ARGUMENT :
                                BOOT_FLASH_STATUS_RANGE;
    }

    storage = fake_region_storage(region, &region_size);
    (void)region_size;
    memcpy(data, &storage[offset], length);
    return BOOT_FLASH_STATUS_OK;
}

void boot_platform_prepare_handoff(void)
{
}

void boot_platform_jump(uint32_t vector_address,
                        uint32_t initial_stack_pointer,
                        uint32_t reset_handler)
{
    (void)vector_address;
    (void)initial_stack_pointer;
    (void)reset_handler;
    abort();
}

void boot_platform_reset(void)
{
    abort();
}

static void metadata_set_image_size(uint32_t image_size)
{
    boot_protocol_write_u32_le(&metadata_flash[8], image_size);
    boot_protocol_write_u32_le(
        &metadata_flash[TEST_METADATA_HEADER_CRC_OFFSET],
        boot_crc32_compute(metadata_flash, TEST_METADATA_CRC_INPUT_SIZE));
}

static void fill_marker(size_t offset, uint8_t value)
{
    memset(&metadata_flash[offset], value, BOOT_FLASH_PROGRAM_UNIT);
}

static void build_valid_image(uint32_t image_size)
{
    uint32_t index;

    assert(image_size >= 8U);
    assert(image_size <= BOOT_APPLICATION_MAX_SIZE);
    for (index = 0U; index < image_size; ++index)
    {
        application_flash[index] = (uint8_t)((index * UINT32_C(37)) + UINT32_C(11));
    }
    boot_protocol_write_u32_le(&application_flash[0], BOOT_SRAM_END_ADDRESS);
    boot_protocol_write_u32_le(&application_flash[4],
                               BOOT_APPLICATION_ADDRESS | UINT32_C(1));
}

static void assert_metadata_state(boot_metadata_state_t expected_state,
                                  uint32_t expected_size,
                                  uint32_t expected_crc)
{
    boot_metadata_t metadata = {0};

    assert(boot_metadata_read(&metadata));
    assert(metadata.state == expected_state);
    assert(metadata.image_size == expected_size);
    assert(metadata.image_crc32 == expected_crc);
    assert(metadata.image_version == 0U);
}

static void test_metadata_header_little_endian_and_crc(void)
{
    static const uint8_t expected_crc_input[TEST_METADATA_CRC_INPUT_SIZE] = {
        0x43U, 0x4CU, 0x4DU, 0x44U,
        0x01U, 0x00U,
        BOOT_PROTOCOL_VERSION, 0x00U,
        0x34U, 0x12U, 0x00U, 0x00U,
        0xEFU, 0xCDU, 0xABU, 0x89U,
        0x00U, 0x00U, 0x00U, 0x00U
    };
    const uint32_t image_size = UINT32_C(0x1234);
    const uint32_t image_crc = UINT32_C(0x89ABCDEF);
    const uint32_t expected_header_crc =
        boot_crc32_compute(expected_crc_input, sizeof(expected_crc_input));
    size_t index;

    fake_flash_reset();
    assert(boot_metadata_begin(image_size, image_crc) == BOOT_FLASH_STATUS_OK);

    assert(fake_flash.erase_calls == 1U);
    assert(fake_flash.erases[0].region == BOOT_FLASH_REGION_METADATA);
    assert(fake_flash.erases[0].offset == 0U);
    assert(fake_flash.erases[0].length == BOOT_METADATA_PAGE_SIZE);
    assert(fake_flash.write_calls == 1U);
    assert(fake_flash.writes[0].region == BOOT_FLASH_REGION_METADATA);
    assert(fake_flash.writes[0].offset == 0U);
    assert(fake_flash.writes[0].length == TEST_METADATA_SERIALIZED_SIZE);

    assert(memcmp(metadata_flash,
                  expected_crc_input,
                  sizeof(expected_crc_input)) == 0);
    assert(boot_protocol_read_u32_le(
               &metadata_flash[TEST_METADATA_HEADER_CRC_OFFSET]) ==
           expected_header_crc);
    assert(metadata_flash[20] == (uint8_t)expected_header_crc);
    assert(metadata_flash[21] == (uint8_t)(expected_header_crc >> 8U));
    assert(metadata_flash[22] == (uint8_t)(expected_header_crc >> 16U));
    assert(metadata_flash[23] == (uint8_t)(expected_header_crc >> 24U));
    assert(metadata_flash[TEST_METADATA_UPDATE_MARKER_OFFSET] == 0U);
    assert(metadata_flash[TEST_METADATA_UPDATE_MARKER_OFFSET + 1U] == 0U);
    assert(metadata_flash[TEST_METADATA_VALID_MARKER_OFFSET] == UINT8_MAX);
    assert(metadata_flash[TEST_METADATA_VALID_MARKER_OFFSET + 1U] == UINT8_MAX);
    assert(metadata_flash[TEST_METADATA_INVALID_MARKER_OFFSET] == UINT8_MAX);
    assert(metadata_flash[TEST_METADATA_INVALID_MARKER_OFFSET + 1U] == UINT8_MAX);
    assert_metadata_state(BOOT_METADATA_STATE_UPDATING, image_size, image_crc);

    for (index = 0U; index < TEST_METADATA_CRC_INPUT_SIZE; ++index)
    {
        metadata_flash[index] ^= UINT8_C(1);
        assert(!boot_metadata_read(&(boot_metadata_t){0}));
        metadata_flash[index] ^= UINT8_C(1);
    }
    for (index = TEST_METADATA_HEADER_CRC_OFFSET;
         index < TEST_METADATA_HEADER_SIZE;
         ++index)
    {
        metadata_flash[index] ^= UINT8_C(1);
        assert(!boot_metadata_read(&(boot_metadata_t){0}));
        metadata_flash[index] ^= UINT8_C(1);
    }
}

static void test_metadata_marker_transitions_and_precedence(void)
{
    static const uint32_t image_size = 256U;
    static const uint32_t image_crc = UINT32_C(0x13579BDF);

    fake_flash_reset();
    assert(boot_metadata_begin(image_size, image_crc) == BOOT_FLASH_STATUS_OK);
    assert_metadata_state(BOOT_METADATA_STATE_UPDATING, image_size, image_crc);

    fake_flash_clear_log();
    assert(boot_metadata_mark_valid() == BOOT_FLASH_STATUS_OK);
    assert(fake_flash.write_calls == 1U);
    assert(fake_flash.writes[0].region == BOOT_FLASH_REGION_METADATA);
    assert(fake_flash.writes[0].offset == TEST_METADATA_VALID_MARKER_OFFSET);
    assert(fake_flash.writes[0].length == BOOT_FLASH_PROGRAM_UNIT);
    assert(metadata_flash[TEST_METADATA_UPDATE_MARKER_OFFSET] == 0U);
    assert(metadata_flash[TEST_METADATA_VALID_MARKER_OFFSET] == 0U);
    assert(metadata_flash[TEST_METADATA_INVALID_MARKER_OFFSET] == UINT8_MAX);
    assert_metadata_state(BOOT_METADATA_STATE_VALID, image_size, image_crc);

    fake_flash_clear_log();
    assert(boot_metadata_mark_invalid() == BOOT_FLASH_STATUS_OK);
    assert(fake_flash.write_calls == 1U);
    assert(fake_flash.writes[0].region == BOOT_FLASH_REGION_METADATA);
    assert(fake_flash.writes[0].offset == TEST_METADATA_INVALID_MARKER_OFFSET);
    assert(fake_flash.writes[0].length == BOOT_FLASH_PROGRAM_UNIT);
    assert(metadata_flash[TEST_METADATA_UPDATE_MARKER_OFFSET] == 0U);
    assert(metadata_flash[TEST_METADATA_VALID_MARKER_OFFSET] == 0U);
    assert(metadata_flash[TEST_METADATA_INVALID_MARKER_OFFSET] == 0U);
    assert_metadata_state(BOOT_METADATA_STATE_INVALID, image_size, image_crc);

    fake_flash_clear_log();
    assert(boot_metadata_mark_invalid() == BOOT_FLASH_STATUS_OK);
    assert(fake_flash.write_calls == 0U);
}

static void test_metadata_rejects_torn_and_unknown_markers(void)
{
    static const size_t marker_offsets[] = {
        TEST_METADATA_UPDATE_MARKER_OFFSET,
        TEST_METADATA_VALID_MARKER_OFFSET,
        TEST_METADATA_INVALID_MARKER_OFFSET
    };
    uint8_t baseline[TEST_METADATA_SERIALIZED_SIZE];
    size_t marker;

    fake_flash_reset();
    assert(boot_metadata_begin(64U, UINT32_C(0x2468ACE0)) ==
           BOOT_FLASH_STATUS_OK);
    memcpy(baseline, metadata_flash, sizeof(baseline));

    for (marker = 0U;
         marker < (sizeof(marker_offsets) / sizeof(marker_offsets[0]));
         ++marker)
    {
        const size_t offset = marker_offsets[marker];

        memcpy(metadata_flash, baseline, sizeof(baseline));
        metadata_flash[offset] = 0U;
        metadata_flash[offset + 1U] = UINT8_MAX;
        assert(!boot_metadata_read(&(boot_metadata_t){0}));

        memcpy(metadata_flash, baseline, sizeof(baseline));
        fill_marker(offset, UINT8_C(0xA5));
        assert(!boot_metadata_read(&(boot_metadata_t){0}));
    }
}

static void test_metadata_bounds_and_flash_failures(void)
{
    boot_metadata_t metadata;

    fake_flash_reset();
    assert(!boot_metadata_read(NULL));
    assert(boot_metadata_begin(7U, 0U) == BOOT_FLASH_STATUS_RANGE);
    assert(boot_metadata_begin(BOOT_APPLICATION_MAX_SIZE + 1U, 0U) ==
           BOOT_FLASH_STATUS_RANGE);
    assert(fake_flash.erase_calls == 0U);
    assert(fake_flash.write_calls == 0U);

    assert(boot_metadata_begin(8U, UINT32_C(0x11223344)) ==
           BOOT_FLASH_STATUS_OK);
    metadata_set_image_size(7U);
    assert(!boot_metadata_read(&metadata));
    metadata_set_image_size(BOOT_APPLICATION_MAX_SIZE + 1U);
    assert(!boot_metadata_read(&metadata));
    metadata_set_image_size(BOOT_APPLICATION_MAX_SIZE);
    assert(boot_metadata_read(&metadata));
    assert(metadata.image_size == BOOT_APPLICATION_MAX_SIZE);

    fake_flash_reset();
    fake_flash.erase_status = BOOT_FLASH_STATUS_ERASE;
    assert(boot_metadata_begin(8U, 0U) == BOOT_FLASH_STATUS_ERASE);
    assert(fake_flash.write_calls == 0U);

    fake_flash_reset();
    fake_flash.write_status = BOOT_FLASH_STATUS_PROGRAM;
    assert(boot_metadata_begin(8U, 0U) == BOOT_FLASH_STATUS_PROGRAM);

    fake_flash_reset();
    assert(boot_metadata_begin(8U, 0U) == BOOT_FLASH_STATUS_OK);
    fake_flash.read_status = BOOT_FLASH_STATUS_HARDWARE;
    assert(!boot_metadata_read(&metadata));
    assert(boot_metadata_mark_valid() == BOOT_FLASH_STATUS_VERIFY);

    fake_flash.read_status = BOOT_FLASH_STATUS_OK;
    fake_flash.write_status = BOOT_FLASH_STATUS_PROGRAM;
    assert(boot_metadata_mark_valid() == BOOT_FLASH_STATUS_PROGRAM);
    fake_flash.write_status = BOOT_FLASH_STATUS_OK;
    assert_metadata_state(BOOT_METADATA_STATE_UPDATING, 8U, 0U);
}

static void test_image_crc_bounds_and_chunking(void)
{
    static const uint32_t image_size = 257U;
    uint32_t crc = UINT32_C(0xDEADBEEF);
    uint32_t expected_crc;

    fake_flash_reset();
    build_valid_image(image_size);
    expected_crc = boot_crc32_compute(application_flash, image_size);
    fake_flash_clear_log();

    assert(boot_image_calculate_crc(image_size, &crc));
    assert(crc == expected_crc);
    assert(fake_flash.read_calls == 3U);
    assert(fake_flash.reads[0].region == BOOT_FLASH_REGION_APPLICATION);
    assert(fake_flash.reads[0].offset == 0U);
    assert(fake_flash.reads[0].length == 128U);
    assert(fake_flash.reads[1].offset == 128U);
    assert(fake_flash.reads[1].length == 128U);
    assert(fake_flash.reads[2].offset == 256U);
    assert(fake_flash.reads[2].length == 1U);

    fake_flash_clear_log();
    assert(!boot_image_calculate_crc(7U, &crc));
    assert(!boot_image_calculate_crc(BOOT_APPLICATION_MAX_SIZE + 1U, &crc));
    assert(!boot_image_calculate_crc(8U, NULL));
    assert(fake_flash.read_calls == 0U);

    fake_flash.read_status = BOOT_FLASH_STATUS_HARDWARE;
    assert(!boot_image_calculate_crc(8U, &crc));
}

static bool vectors_are_valid(uint32_t image_size,
                              uint32_t stack_pointer,
                              uint32_t reset_handler)
{
    uint32_t loaded_stack = UINT32_C(0xAAAAAAAA);
    uint32_t loaded_handler = UINT32_C(0xBBBBBBBB);
    bool valid;

    boot_protocol_write_u32_le(&application_flash[0], stack_pointer);
    boot_protocol_write_u32_le(&application_flash[4], reset_handler);
    valid = boot_image_vectors_valid(image_size, &loaded_stack, &loaded_handler);
    if (valid)
    {
        assert(loaded_stack == stack_pointer);
        assert(loaded_handler == reset_handler);
    }
    return valid;
}

static void test_image_vector_validation(void)
{
    static const uint32_t image_size = 16U;
    uint32_t stack_pointer;
    uint32_t reset_handler;

    fake_flash_reset();
    assert(vectors_are_valid(image_size,
                             BOOT_SRAM_END_ADDRESS,
                             BOOT_APPLICATION_ADDRESS | UINT32_C(1)));
    assert(vectors_are_valid(8U,
                             BOOT_SRAM_START_ADDRESS + 8U,
                             BOOT_APPLICATION_ADDRESS | UINT32_C(1)));

    assert(!vectors_are_valid(image_size,
                              BOOT_SRAM_START_ADDRESS,
                              BOOT_APPLICATION_ADDRESS | UINT32_C(1)));
    assert(!vectors_are_valid(image_size,
                              BOOT_SRAM_START_ADDRESS + 4U,
                              BOOT_APPLICATION_ADDRESS | UINT32_C(1)));
    assert(!vectors_are_valid(image_size,
                              BOOT_SRAM_END_ADDRESS + 8U,
                              BOOT_APPLICATION_ADDRESS | UINT32_C(1)));
    assert(!vectors_are_valid(image_size,
                              BOOT_SRAM_END_ADDRESS,
                              BOOT_APPLICATION_ADDRESS));
    assert(!vectors_are_valid(image_size,
                              BOOT_SRAM_END_ADDRESS,
                              (BOOT_APPLICATION_ADDRESS - 2U) | UINT32_C(1)));
    assert(!vectors_are_valid(image_size,
                              BOOT_SRAM_END_ADDRESS,
                              (BOOT_APPLICATION_ADDRESS + image_size) |
                                  UINT32_C(1)));
    assert(vectors_are_valid(image_size,
                             BOOT_SRAM_END_ADDRESS,
                             (BOOT_APPLICATION_ADDRESS + image_size - 2U) |
                                 UINT32_C(1)));

    fake_flash_clear_log();
    assert(!boot_image_vectors_valid(7U, &stack_pointer, &reset_handler));
    assert(!boot_image_vectors_valid(BOOT_APPLICATION_MAX_SIZE + 1U,
                                     &stack_pointer,
                                     &reset_handler));
    assert(!boot_image_vectors_valid(8U, NULL, &reset_handler));
    assert(!boot_image_vectors_valid(8U, &stack_pointer, NULL));
    assert(fake_flash.read_calls == 0U);

    fake_flash.read_status = BOOT_FLASH_STATUS_HARDWARE;
    assert(!boot_image_vectors_valid(8U, &stack_pointer, &reset_handler));
}

static void test_candidate_and_persisted_image_validation(void)
{
    static const uint32_t image_size = 260U;
    boot_metadata_t metadata = {0};
    uint32_t image_crc;

    fake_flash_reset();
    build_valid_image(image_size);
    image_crc = boot_crc32_compute(application_flash, image_size);
    assert(boot_image_validate_candidate(image_size, image_crc));
    assert(!boot_image_validate_candidate(image_size, image_crc ^ UINT32_C(1)));
    assert(!boot_image_validate_candidate(7U, image_crc));
    assert(!boot_image_validate_candidate(BOOT_APPLICATION_MAX_SIZE + 1U,
                                          image_crc));

    boot_protocol_write_u32_le(&application_flash[4], BOOT_APPLICATION_ADDRESS);
    assert(!boot_image_validate_candidate(
        image_size,
        boot_crc32_compute(application_flash, image_size)));

    fake_flash_reset();
    build_valid_image(image_size);
    image_crc = boot_crc32_compute(application_flash, image_size);
    assert(boot_metadata_begin(image_size, image_crc) == BOOT_FLASH_STATUS_OK);
    assert(!boot_image_validate(&metadata));
    assert(boot_metadata_mark_valid() == BOOT_FLASH_STATUS_OK);
    assert(boot_image_validate(&metadata));
    assert(metadata.state == BOOT_METADATA_STATE_VALID);
    assert(metadata.image_size == image_size);
    assert(metadata.image_crc32 == image_crc);
    assert(boot_image_validate(NULL));

    assert(boot_metadata_mark_invalid() == BOOT_FLASH_STATUS_OK);
    assert_metadata_state(BOOT_METADATA_STATE_INVALID, image_size, image_crc);
    assert(!boot_image_validate(&metadata));

    fake_flash_reset();
    build_valid_image(image_size);
    image_crc = boot_crc32_compute(application_flash, image_size);
    assert(boot_metadata_begin(image_size, image_crc ^ UINT32_C(1)) ==
           BOOT_FLASH_STATUS_OK);
    assert(boot_metadata_mark_valid() == BOOT_FLASH_STATUS_OK);
    assert(!boot_image_validate(&metadata));

    fake_flash.read_status = BOOT_FLASH_STATUS_HARDWARE;
    assert(!boot_image_validate_candidate(image_size, image_crc));
}

int main(void)
{
    test_metadata_header_little_endian_and_crc();
    test_metadata_marker_transitions_and_precedence();
    test_metadata_rejects_torn_and_unknown_markers();
    test_metadata_bounds_and_flash_failures();
    test_image_crc_bounds_and_chunking();
    test_image_vector_validation();
    test_candidate_and_persisted_image_validation();

    puts("test_metadata_image: OK");
    return 0;
}
