#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boot_can.h"
#include "boot_config.h"
#include "boot_device_config.h"
#include "boot_flash.h"
#include "boot_image.h"
#include "boot_metadata.h"
#include "boot_platform.h"
#include "boot_protocol.h"
#include "bootloader.h"

#define FAKE_RX_CAPACITY 32U
#define FAKE_TX_CAPACITY 64U

typedef struct
{
    uint32_t now_ms;
    bool can_init_ok;
    bool can_start_ok;
    boot_can_status_t send_status;
    boot_flash_status_t flash_init_status;
    boot_flash_status_t erase_status;
    boot_flash_status_t write_status;
    boot_flash_status_t metadata_begin_status;
    boot_flash_status_t metadata_mark_valid_status;
    boot_flash_status_t metadata_mark_invalid_status;
    boot_flash_status_t device_config_store_status;
    uint8_t configured_node;
    uint8_t can_init_node;
    uint8_t stored_node;
    bool boot_request;
    bool boot_request_set;
    uint32_t uid_words[3];
    bool image_valid;
    bool metadata_read_ok;
    boot_metadata_t metadata;

    boot_can_frame_t rx_frames[FAKE_RX_CAPACITY];
    size_t rx_head;
    size_t rx_count;
    boot_can_frame_t tx_frames[FAKE_TX_CAPACITY];
    size_t tx_count;

    size_t can_stop_calls;
    size_t led_set_calls;
    bool last_led_value;

    size_t erase_calls;
    boot_flash_region_t erase_region;
    uint32_t erase_offset;
    size_t erase_length;

    size_t write_calls;
    boot_flash_region_t write_region;
    uint32_t write_offset;
    size_t write_length;
    uint8_t write_data[BOOT_TRANSFER_BLOCK_SIZE + 1U];

    size_t metadata_begin_calls;
    uint32_t metadata_begin_size;
    uint32_t metadata_begin_crc;
    size_t metadata_mark_valid_calls;
    size_t metadata_mark_invalid_calls;
} fake_environment_t;

static fake_environment_t fake;
static jmp_buf jump_environment;
static bool capture_jump;
static size_t jump_calls;

static void fake_reset(void)
{
    fake = (fake_environment_t){
        .can_init_ok = true,
        .can_start_ok = true,
        .send_status = BOOT_CAN_STATUS_OK,
        .flash_init_status = BOOT_FLASH_STATUS_OK,
        .erase_status = BOOT_FLASH_STATUS_OK,
        .write_status = BOOT_FLASH_STATUS_OK,
        .metadata_begin_status = BOOT_FLASH_STATUS_OK,
        .metadata_mark_valid_status = BOOT_FLASH_STATUS_OK,
        .metadata_mark_invalid_status = BOOT_FLASH_STATUS_OK,
        .device_config_store_status = BOOT_FLASH_STATUS_OK,
        .configured_node = BOOT_DEFAULT_NODE_ID,
        .uid_words = {
            UINT32_C(0x11223344),
            UINT32_C(0x55667788),
            UINT32_C(0x99AABBCC)
        },
        .image_valid = false,
        .metadata_read_ok = false
    };
    capture_jump = false;
    jump_calls = 0U;
}

static void enqueue_rx(const boot_can_frame_t *frame)
{
    assert(frame != NULL);
    assert(fake.rx_count < FAKE_RX_CAPACITY);
    fake.rx_frames[fake.rx_count] = *frame;
    ++fake.rx_count;
}

bool boot_can_init(uint8_t node_id)
{
    fake.can_init_node = node_id;
    return fake.can_init_ok;
}

bool boot_can_start(void)
{
    return fake.can_start_ok;
}

void boot_can_stop(void)
{
    ++fake.can_stop_calls;
}

boot_can_status_t boot_can_send(const boot_can_frame_t *frame)
{
    assert(frame != NULL);
    if (fake.send_status == BOOT_CAN_STATUS_OK)
    {
        assert(fake.tx_count < FAKE_TX_CAPACITY);
        fake.tx_frames[fake.tx_count] = *frame;
        ++fake.tx_count;
    }
    return fake.send_status;
}

boot_can_status_t boot_can_receive(boot_can_frame_t *frame)
{
    assert(frame != NULL);
    if (fake.rx_head == fake.rx_count)
    {
        return BOOT_CAN_STATUS_EMPTY;
    }

    *frame = fake.rx_frames[fake.rx_head];
    ++fake.rx_head;
    return BOOT_CAN_STATUS_OK;
}

bool boot_can_tx_idle(void)
{
    return true;
}

boot_flash_status_t boot_flash_init(void)
{
    return fake.flash_init_status;
}

boot_flash_status_t boot_flash_erase(boot_flash_region_t region,
                                     uint32_t offset,
                                     size_t length)
{
    ++fake.erase_calls;
    fake.erase_region = region;
    fake.erase_offset = offset;
    fake.erase_length = length;
    return fake.erase_status;
}

boot_flash_status_t boot_flash_write(boot_flash_region_t region,
                                     uint32_t offset,
                                     const uint8_t *data,
                                     size_t length)
{
    assert(data != NULL);
    assert(length <= sizeof(fake.write_data));
    ++fake.write_calls;
    fake.write_region = region;
    fake.write_offset = offset;
    fake.write_length = length;
    memcpy(fake.write_data, data, length);
    return fake.write_status;
}

bool boot_metadata_read(boot_metadata_t *metadata)
{
    assert(metadata != NULL);
    if (!fake.metadata_read_ok)
    {
        return false;
    }
    *metadata = fake.metadata;
    return true;
}

boot_flash_status_t boot_metadata_begin(uint32_t image_size, uint32_t image_crc32)
{
    ++fake.metadata_begin_calls;
    fake.metadata_begin_size = image_size;
    fake.metadata_begin_crc = image_crc32;
    return fake.metadata_begin_status;
}

boot_flash_status_t boot_metadata_mark_valid(void)
{
    ++fake.metadata_mark_valid_calls;
    return fake.metadata_mark_valid_status;
}

boot_flash_status_t boot_metadata_mark_invalid(void)
{
    ++fake.metadata_mark_invalid_calls;
    return fake.metadata_mark_invalid_status;
}

bool boot_device_config_read(uint8_t *node_id)
{
    assert(node_id != NULL);
    *node_id = fake.configured_node;
    return true;
}

uint8_t boot_device_config_load_node(void)
{
    return fake.configured_node;
}

boot_flash_status_t boot_device_config_store_node(uint8_t node_id)
{
    fake.stored_node = node_id;
    return fake.device_config_store_status;
}

bool boot_image_calculate_crc(uint32_t image_size, uint32_t *crc32)
{
    (void)image_size;
    assert(crc32 != NULL);
    *crc32 = fake.metadata.image_crc32;
    return true;
}

bool boot_image_vectors_valid(uint32_t image_size,
                              uint32_t *initial_stack_pointer,
                              uint32_t *reset_handler)
{
    (void)image_size;
    assert(initial_stack_pointer != NULL);
    assert(reset_handler != NULL);
    *initial_stack_pointer = BOOT_SRAM_START_ADDRESS;
    *reset_handler = BOOT_APPLICATION_ADDRESS | UINT32_C(1);
    return true;
}

bool boot_image_validate(boot_metadata_t *metadata)
{
    if (metadata != NULL)
    {
        *metadata = fake.metadata;
    }
    return fake.image_valid;
}

void boot_image_jump(void)
{
    ++jump_calls;
    if (capture_jump)
    {
        longjmp(jump_environment, 1);
    }
    abort();
}

uint32_t boot_platform_millis(void)
{
    return fake.now_ms;
}

void boot_platform_led_set(bool on)
{
    ++fake.led_set_calls;
    fake.last_led_value = on;
}

uint32_t boot_platform_uid_word(uint8_t index)
{
    assert(index < 3U);
    return fake.uid_words[index];
}

bool boot_platform_take_boot_request(void)
{
    const bool requested = fake.boot_request;
    fake.boot_request = false;
    return requested;
}

void boot_platform_set_boot_request(void)
{
    fake.boot_request_set = true;
}

void boot_platform_reset(void)
{
    abort();
}

static boot_can_frame_t make_request(boot_command_t command,
                                     uint8_t session,
                                     uint8_t parameter,
                                     uint32_t value)
{
    boot_can_frame_t frame = {0};

    frame.id = BOOT_CAN_COMMAND_ID(fake.configured_node);
    frame.dlc = BOOT_CAN_MAX_DATA_LENGTH;
    frame.data[0] = BOOT_PROTOCOL_VERSION;
    frame.data[1] = (uint8_t)command;
    frame.data[2] = session;
    frame.data[3] = parameter;
    boot_protocol_write_u32_le(&frame.data[4], value);
    return frame;
}

static boot_can_frame_t make_begin_block(uint8_t session,
                                         uint16_t block_index,
                                         uint8_t block_sequence,
                                         uint8_t block_length)
{
    const uint32_t value = (uint32_t)block_index |
                           ((uint32_t)block_length << 16U);
    return make_request(BOOT_COMMAND_BEGIN_BLOCK,
                        session,
                        block_sequence,
                        value);
}

static boot_can_frame_t make_data(uint8_t session,
                                  uint8_t block_sequence,
                                  uint8_t frame_sequence,
                                  const uint8_t *payload,
                                  uint8_t payload_length)
{
    boot_can_frame_t frame = {0};

    assert(payload != NULL);
    assert(payload_length >= 1U);
    assert(payload_length <= BOOT_DATA_BYTES_PER_FRAME);
    frame.id = BOOT_CAN_DATA_ID(fake.configured_node);
    frame.dlc = (uint8_t)(3U + payload_length);
    frame.data[0] = session;
    frame.data[1] = block_sequence;
    frame.data[2] = frame_sequence;
    memcpy(&frame.data[3], payload, payload_length);
    return frame;
}

static boot_can_frame_t exchange(const boot_can_frame_t *request)
{
    const size_t tx_before = fake.tx_count;

    enqueue_rx(request);
    bootloader_task();
    assert(fake.tx_count == tx_before);
    bootloader_task();
    assert(fake.tx_count == tx_before + 1U);
    return fake.tx_frames[tx_before];
}

static void inject_without_response(const boot_can_frame_t *frame)
{
    const size_t tx_before = fake.tx_count;

    enqueue_rx(frame);
    bootloader_task();
    bootloader_task();
    assert(fake.tx_count == tx_before);
}

static void assert_response(const boot_can_frame_t *response,
                            boot_response_kind_t kind,
                            boot_command_t command,
                            uint8_t session,
                            uint32_t value)
{
    assert(response != NULL);
    assert(response->id == BOOT_CAN_RESPONSE_ID(fake.configured_node));
    assert(response->dlc == BOOT_CAN_MAX_DATA_LENGTH);
    assert(response->data[0] == BOOT_PROTOCOL_VERSION);
    assert(response->data[1] == (uint8_t)kind);
    assert(response->data[2] == (uint8_t)command);
    assert(response->data[3] == session);
    assert(boot_protocol_read_u32_le(&response->data[4]) == value);
}

static void init_waiting_bootloader(void)
{
    fake_reset();
    bootloader_init();
    assert(bootloader_get_state() == BOOTLOADER_STATE_WAIT_HOST);
}

static void begin_receive(uint8_t session, uint32_t image_size, uint32_t crc32)
{
    boot_can_frame_t request;
    boot_can_frame_t response;

    init_waiting_bootloader();

    request = make_request(BOOT_COMMAND_START_UPDATE, session, 0U, image_size);
    response = exchange(&request);
    assert_response(&response,
                    BOOT_RESPONSE_ACK,
                    BOOT_COMMAND_START_UPDATE,
                    session,
                    BOOT_TRANSFER_BLOCK_SIZE);
    assert(bootloader_get_state() == BOOTLOADER_STATE_PREPARE_UPDATE);

    request = make_request(BOOT_COMMAND_SET_IMAGE_CRC, session, 0U, crc32);
    response = exchange(&request);
    assert_response(&response,
                    BOOT_RESPONSE_ACK,
                    BOOT_COMMAND_SET_IMAGE_CRC,
                    session,
                    crc32);

    request = make_request(BOOT_COMMAND_ERASE, session, 0U, 0U);
    response = exchange(&request);
    assert_response(&response,
                    BOOT_RESPONSE_ACK,
                    BOOT_COMMAND_ERASE,
                    session,
                    image_size);
    assert(bootloader_get_state() == BOOTLOADER_STATE_RECEIVE);
    assert(fake.metadata_begin_calls == 1U);
    assert(fake.metadata_begin_size == image_size);
    assert(fake.metadata_begin_crc == crc32);
    assert(fake.erase_calls == 1U);
    assert(fake.erase_region == BOOT_FLASH_REGION_APPLICATION);
    assert(fake.erase_offset == 0U);
    assert(fake.erase_length ==
           ((image_size + BOOT_FLASH_PAGE_SIZE - 1U) / BOOT_FLASH_PAGE_SIZE) *
               BOOT_FLASH_PAGE_SIZE);
}

static void test_start_update_bounds(void)
{
    static const uint8_t session = UINT8_C(0x11);
    boot_can_frame_t request;
    boot_can_frame_t response;

    init_waiting_bootloader();

    request = make_request(BOOT_COMMAND_START_UPDATE, session, 0U, 7U);
    response = exchange(&request);
    assert_response(&response,
                    BOOT_RESPONSE_NACK,
                    BOOT_COMMAND_START_UPDATE,
                    session,
                    BOOT_ERROR_INVALID_SIZE);
    assert(bootloader_get_state() == BOOTLOADER_STATE_WAIT_HOST);

    request = make_request(BOOT_COMMAND_START_UPDATE,
                           session,
                           0U,
                           BOOT_APPLICATION_MAX_SIZE + 1U);
    response = exchange(&request);
    assert_response(&response,
                    BOOT_RESPONSE_NACK,
                    BOOT_COMMAND_START_UPDATE,
                    session,
                    BOOT_ERROR_INVALID_SIZE);
    assert(fake.erase_calls == 0U);
    assert(fake.metadata_begin_calls == 0U);
}

static void test_sequence_and_bounds_are_rejected(void)
{
    static const uint8_t session = UINT8_C(0x22);
    static const uint8_t short_payload[] = {0xAAU};
    static const uint8_t sequenced_payload[] = {0xAAU, 0xBBU, 0xCCU};
    boot_can_frame_t frame;
    boot_can_frame_t response;

    begin_receive(session, 8U, UINT32_C(0x01020304));

    frame = make_begin_block(session, 1U, 1U, 8U);
    response = exchange(&frame);
    assert_response(&response,
                    BOOT_RESPONSE_NACK,
                    BOOT_COMMAND_BEGIN_BLOCK,
                    session,
                    BOOT_ERROR_INVALID_SEQUENCE);

    frame = make_begin_block(session, 0U, 0U, 7U);
    response = exchange(&frame);
    assert_response(&response,
                    BOOT_RESPONSE_NACK,
                    BOOT_COMMAND_BEGIN_BLOCK,
                    session,
                    BOOT_ERROR_INVALID_SEQUENCE);

    frame = make_begin_block(session, 0U, 0U, 8U);
    response = exchange(&frame);
    assert_response(&response,
                    BOOT_RESPONSE_ACK,
                    BOOT_COMMAND_BEGIN_BLOCK,
                    session,
                    0U);

    frame = make_data(session, 0U, 1U, short_payload, sizeof(short_payload));
    response = exchange(&frame);
    assert_response(&response,
                    BOOT_RESPONSE_NACK,
                    BOOT_COMMAND_DATA,
                    session,
                    BOOT_ERROR_INVALID_SIZE);

    frame = make_data(session,
                      0U,
                      1U,
                      sequenced_payload,
                      sizeof(sequenced_payload));
    response = exchange(&frame);
    assert_response(&response,
                    BOOT_RESPONSE_NACK,
                    BOOT_COMMAND_DATA,
                    session,
                    BOOT_ERROR_INVALID_SEQUENCE);

    frame = make_data(session, 0U, 2U, short_payload, sizeof(short_payload));
    response = exchange(&frame);
    assert_response(&response,
                    BOOT_RESPONSE_NACK,
                    BOOT_COMMAND_DATA,
                    session,
                    BOOT_ERROR_INVALID_SEQUENCE);
    assert(fake.write_calls == 0U);
}

static void test_duplicate_frames_and_blocks_are_idempotent(void)
{
    static const uint8_t session = UINT8_C(0x33);
    static const uint8_t first_payload[] = {1U, 2U, 3U, 4U, 5U};
    static const uint8_t bad_duplicate[] = {1U, 2U, 3U, 4U, 0xEEU};
    static const uint8_t second_payload[] = {6U, 7U, 8U, 9U};
    static const uint8_t expected_image[] = {
        1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U
    };
    boot_can_frame_t frame;
    boot_can_frame_t response;

    begin_receive(session, sizeof(expected_image), UINT32_C(0xAABBCCDD));

    frame = make_begin_block(session, 0U, 0U, sizeof(expected_image));
    response = exchange(&frame);
    assert_response(&response,
                    BOOT_RESPONSE_ACK,
                    BOOT_COMMAND_BEGIN_BLOCK,
                    session,
                    0U);

    frame = make_data(session, 0U, 0U, first_payload, sizeof(first_payload));
    inject_without_response(&frame);
    inject_without_response(&frame);
    assert(fake.write_calls == 0U);

    frame = make_data(session, 0U, 0U, bad_duplicate, sizeof(bad_duplicate));
    response = exchange(&frame);
    assert_response(&response,
                    BOOT_RESPONSE_NACK,
                    BOOT_COMMAND_DATA,
                    session,
                    BOOT_ERROR_INVALID_SEQUENCE);
    assert(fake.write_calls == 0U);

    frame = make_data(session, 0U, 1U, second_payload, sizeof(second_payload));
    response = exchange(&frame);
    assert_response(&response,
                    BOOT_RESPONSE_ACK,
                    BOOT_COMMAND_DATA,
                    session,
                    sizeof(expected_image));
    assert(fake.write_calls == 1U);
    assert(fake.write_region == BOOT_FLASH_REGION_APPLICATION);
    assert(fake.write_offset == 0U);
    assert(fake.write_length == sizeof(expected_image) + 1U);
    assert(memcmp(fake.write_data, expected_image, sizeof(expected_image)) == 0);
    assert(fake.write_data[sizeof(expected_image)] == UINT8_C(0xFF));

    frame = make_begin_block(session, 0U, 0U, sizeof(expected_image));
    response = exchange(&frame);
    assert_response(&response,
                    BOOT_RESPONSE_ACK,
                    BOOT_COMMAND_BEGIN_BLOCK,
                    session,
                    sizeof(expected_image));
    assert(fake.write_calls == 1U);
}

static void test_receive_timeout_invalidates_partial_update(void)
{
    static const uint8_t session = UINT8_C(0x44);
    size_t tx_before_timeout;
    boot_can_frame_t response;

    begin_receive(session, 8U, UINT32_C(0x11223344));
    tx_before_timeout = fake.tx_count;
    fake.now_ms = BOOT_TRANSFER_TIMEOUT_MS;

    bootloader_task();
    assert(bootloader_get_state() == BOOTLOADER_STATE_WAIT_HOST);
    assert(fake.metadata_mark_invalid_calls == 1U);
    assert(fake.tx_count == tx_before_timeout);

    bootloader_task();
    assert(fake.tx_count == tx_before_timeout + 1U);
    response = fake.tx_frames[tx_before_timeout];
    assert_response(&response,
                    BOOT_RESPONSE_NACK,
                    BOOT_COMMAND_ABORT,
                    session,
                    BOOT_ERROR_TIMEOUT);
}

static void test_timeout_replaces_a_stuck_pending_response(void)
{
    static const uint8_t session = UINT8_C(0x45);
    boot_can_frame_t request;
    boot_can_frame_t response;
    size_t tx_before_timeout;

    begin_receive(session, 8U, UINT32_C(0x55667788));
    tx_before_timeout = fake.tx_count;
    fake.send_status = BOOT_CAN_STATUS_BUSY;

    request = make_begin_block(session, 0U, 0U, 8U);
    enqueue_rx(&request);
    bootloader_task();
    assert(fake.tx_count == tx_before_timeout);

    fake.now_ms = BOOT_TRANSFER_TIMEOUT_MS;
    bootloader_task();
    assert(bootloader_get_state() == BOOTLOADER_STATE_WAIT_HOST);
    assert(fake.metadata_mark_invalid_calls == 1U);
    assert(fake.tx_count == tx_before_timeout);

    fake.send_status = BOOT_CAN_STATUS_OK;
    bootloader_task();
    assert(fake.tx_count == tx_before_timeout + 1U);
    response = fake.tx_frames[tx_before_timeout];
    assert_response(&response,
                    BOOT_RESPONSE_NACK,
                    BOOT_COMMAND_ABORT,
                    session,
                    BOOT_ERROR_TIMEOUT);
}

static void test_boot_window_ignores_malformed_and_forbidden_broadcast(void)
{
    static const uint8_t session = UINT8_C(0x46);
    boot_can_frame_t request;
    boot_can_frame_t response;
    size_t tx_before;

    fake_reset();
    fake.image_valid = true;
    bootloader_init();
    assert(bootloader_get_state() == BOOTLOADER_STATE_BOOT_WINDOW);

    request = make_request(BOOT_COMMAND_DISCOVER, session, 0U, 0U);
    request.dlc = BOOT_CAN_MAX_DATA_LENGTH - 1U;
    response = exchange(&request);
    assert_response(&response,
                    BOOT_RESPONSE_NACK,
                    BOOT_COMMAND_DISCOVER,
                    session,
                    BOOT_ERROR_MALFORMED_FRAME);
    assert(bootloader_get_state() == BOOTLOADER_STATE_BOOT_WINDOW);

    request = make_request(BOOT_COMMAND_START_UPDATE, session, 0U, 8U);
    request.id = BOOT_CAN_BROADCAST_ID;
    tx_before = fake.tx_count;
    enqueue_rx(&request);
    bootloader_task();
    assert(fake.tx_count == tx_before);
    assert(bootloader_get_state() == BOOTLOADER_STATE_BOOT_WINDOW);

    request = make_request(BOOT_COMMAND_DISCOVER, session, 0U, 0U);
    response = exchange(&request);
    assert_response(&response,
                    BOOT_RESPONSE_ACK,
                    BOOT_COMMAND_DISCOVER,
                    session,
                    UINT32_C(1) |
                        ((uint32_t)BOOT_DEFAULT_NODE_ID << 8U) |
                        ((uint32_t)BOOT_PROTOCOL_VERSION << 16U) |
                        ((uint32_t)BOOT_TARGET_CODE << 24U));
    assert(bootloader_get_state() == BOOTLOADER_STATE_WAIT_HOST);
}

static void test_metadata_begin_failure_clears_advertised_app_validity(void)
{
    static const uint8_t session = UINT8_C(0x47);
    boot_can_frame_t request;
    boot_can_frame_t response;
    uint32_t capabilities;

    fake_reset();
    fake.image_valid = true;
    fake.metadata.image_crc32 = UINT32_C(0x12345678);
    bootloader_init();

    request = make_request(BOOT_COMMAND_START_UPDATE, session, 0U, 8U);
    (void)exchange(&request);
    request = make_request(BOOT_COMMAND_SET_IMAGE_CRC,
                           session,
                           0U,
                           UINT32_C(0x87654321));
    (void)exchange(&request);

    fake.metadata_begin_status = BOOT_FLASH_STATUS_ERASE;
    request = make_request(BOOT_COMMAND_ERASE, session, 0U, 0U);
    response = exchange(&request);
    assert_response(&response,
                    BOOT_RESPONSE_NACK,
                    BOOT_COMMAND_ERASE,
                    session,
                    BOOT_ERROR_FLASH_ERASE);

    request = make_request(BOOT_COMMAND_DISCOVER, session, 0U, 0U);
    response = exchange(&request);
    capabilities = boot_protocol_read_u32_le(&response.data[4]);
    assert((capabilities & UINT32_C(1)) == 0U);
}

static void test_run_timeout_forces_handoff_when_ack_cannot_be_queued(void)
{
    static const uint8_t session = UINT8_C(0x55);
    boot_can_frame_t request;

    fake_reset();
    fake.image_valid = true;
    fake.metadata.image_crc32 = UINT32_C(0x89ABCDEF);
    bootloader_init();
    assert(bootloader_get_state() == BOOTLOADER_STATE_BOOT_WINDOW);

    fake.send_status = BOOT_CAN_STATUS_BUSY;
    request = make_request(BOOT_COMMAND_RUN_APPLICATION, session, 0U, 0U);
    enqueue_rx(&request);
    bootloader_task();
    assert(bootloader_get_state() == BOOTLOADER_STATE_RUN_APPLICATION);
    assert(fake.tx_count == 0U);

    fake.now_ms = BOOT_RUN_RESPONSE_TIMEOUT_MS;
    capture_jump = true;
    if (setjmp(jump_environment) == 0)
    {
        bootloader_task();
        assert(false);
    }
    capture_jump = false;

    assert(jump_calls == 1U);
    assert(fake.can_stop_calls == 1U);
    assert(fake.tx_count == 0U);
}

static void test_dynamic_node_probe_uid_and_reconfiguration(void)
{
    static const uint8_t session = UINT8_C(0x66);
    boot_can_frame_t request;
    boot_can_frame_t response;
    uint32_t capabilities;

    fake_reset();
    fake.configured_node = 42U;
    fake.image_valid = true;
    bootloader_init();
    assert(fake.can_init_node == 42U);
    assert(bootloader_get_state() == BOOTLOADER_STATE_BOOT_WINDOW);

    request = make_request(BOOT_COMMAND_PROBE,
                           session,
                           BOOT_PROBE_CAPABILITIES,
                           0U);
    request.id = BOOT_CAN_BROADCAST_ID;
    response = exchange(&request);
    capabilities = boot_protocol_read_u32_le(&response.data[4]);
    assert_response(&response,
                    BOOT_RESPONSE_ACK,
                    BOOT_COMMAND_PROBE,
                    session,
                    capabilities);
    assert(((capabilities >> 8U) & UINT32_C(0xFF)) == 42U);
    assert(bootloader_get_state() == BOOTLOADER_STATE_BOOT_WINDOW);

    request = make_request(BOOT_COMMAND_INFO,
                           session,
                           0U,
                           BOOT_INFO_UID_WORD_2);
    response = exchange(&request);
    assert_response(&response,
                    BOOT_RESPONSE_ACK,
                    BOOT_COMMAND_INFO,
                    session,
                    fake.uid_words[2]);
    assert(bootloader_get_state() == BOOTLOADER_STATE_WAIT_HOST);

    request = make_request(BOOT_COMMAND_SET_NODE_ID, session, 0U, 7U);
    response = exchange(&request);
    assert_response(&response,
                    BOOT_RESPONSE_ACK,
                    BOOT_COMMAND_SET_NODE_ID,
                    session,
                    7U);
    assert(fake.stored_node == 7U);
    assert(fake.boot_request_set);
    assert(bootloader_get_state() == BOOTLOADER_STATE_RESET);
}

static void test_application_boot_request_extends_window(void)
{
    fake_reset();
    fake.image_valid = true;
    fake.boot_request = true;
    bootloader_init();
    assert(bootloader_get_state() == BOOTLOADER_STATE_BOOT_WINDOW);
    assert(!fake.boot_request);

    fake.now_ms = BOOT_WINDOW_MS;
    bootloader_task();
    assert(bootloader_get_state() == BOOTLOADER_STATE_BOOT_WINDOW);
    assert(jump_calls == 0U);

    fake.now_ms = BOOT_REQUEST_WINDOW_MS;
    capture_jump = true;
    if (setjmp(jump_environment) == 0)
    {
        bootloader_task();
        assert(false);
    }
    capture_jump = false;
    assert(jump_calls == 1U);
}

static void test_enter_bootloader_command_is_idempotent(void)
{
    static const uint8_t session = UINT8_C(0x67);
    boot_can_frame_t request;
    boot_can_frame_t response;

    fake_reset();
    fake.image_valid = true;
    bootloader_init();
    request = make_request(BOOT_COMMAND_ENTER_BOOTLOADER, session, 0U, 0U);
    response = exchange(&request);
    assert_response(&response,
                    BOOT_RESPONSE_ACK,
                    BOOT_COMMAND_ENTER_BOOTLOADER,
                    session,
                    0U);
    assert(bootloader_get_state() == BOOTLOADER_STATE_WAIT_HOST);
}

int main(void)
{
    test_start_update_bounds();
    test_sequence_and_bounds_are_rejected();
    test_duplicate_frames_and_blocks_are_idempotent();
    test_receive_timeout_invalidates_partial_update();
    test_timeout_replaces_a_stuck_pending_response();
    test_boot_window_ignores_malformed_and_forbidden_broadcast();
    test_metadata_begin_failure_clears_advertised_app_validity();
    test_run_timeout_forces_handoff_when_ack_cannot_be_queued();
    test_dynamic_node_probe_uid_and_reconfiguration();
    test_application_boot_request_extends_window();
    test_enter_bootloader_command_is_idempotent();

    puts("test_bootloader: OK");
    return 0;
}
