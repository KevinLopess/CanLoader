#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "boot_can.h"
#include "boot_config.h"
#include "boot_device_config.h"
#include "boot_flash.h"
#include "boot_image.h"
#include "boot_j1939.h"
#include "boot_j1939_tp.h"
#include "boot_metadata.h"
#include "boot_platform.h"
#include "boot_protocol.h"
#include "bootloader.h"

#define HOST_ADDRESS UINT8_C(0xF9)
#define OTHER_HOST_ADDRESS UINT8_C(0xF8)
#define TARGET_NAME UINT64_C(0x8000000000123456)
#define RX_CAPACITY 64U
#define TX_CAPACITY 128U

typedef struct
{
    uint32_t now_ms;
    bool can_init_ok;
    bool can_start_ok;
    bool can_configure_ok;
    bool can_abort_ok;
    bool tx_idle;
    bool image_valid;
    bool metadata_read_ok;
    bool vectors_valid;
    bool crc_ok;
    uint32_t calculated_crc;
    uint8_t configured_address;
    uint8_t reconfigured_address;
    boot_device_config_t config;
    boot_metadata_t metadata;
    boot_can_frame_t rx[RX_CAPACITY];
    size_t rx_head;
    size_t rx_count;
    boot_can_frame_t tx[TX_CAPACITY];
    size_t tx_count;
    size_t can_stop_calls;
    size_t can_abort_calls;
    size_t erase_calls;
    size_t write_calls;
    size_t metadata_begin_calls;
    size_t metadata_mark_valid_calls;
    size_t metadata_mark_invalid_calls;
    uint32_t erase_offset;
    uint32_t erase_duration_ms;
    size_t erase_length;
    uint32_t write_offset;
    size_t write_length;
    uint8_t write_data[BOOT_TRANSFER_BLOCK_SIZE + 1U];
    uint32_t metadata_size;
    uint32_t metadata_crc;
    bool boot_request;
} fake_environment_t;

static fake_environment_t fake;

static void fake_reset(void)
{
    fake = (fake_environment_t){
        .can_init_ok = true,
        .can_start_ok = true,
        .can_configure_ok = true,
        .can_abort_ok = true,
        .tx_idle = true,
        .vectors_valid = true,
        .crc_ok = true,
        .config = {
            .name = TARGET_NAME,
            .generation = 1U,
            .flags = 0U,
            .preferred_address = BOOT_J1939_DEFAULT_PREFERRED_ADDRESS
        }
    };
}

static void enqueue(const boot_can_frame_t *frame)
{
    assert(frame != NULL);
    assert(fake.rx_count < RX_CAPACITY);
    fake.rx[fake.rx_count++] = *frame;
}

bool boot_can_init(uint8_t local_address)
{
    fake.configured_address = local_address;
    return fake.can_init_ok;
}

bool boot_can_configure_address(uint8_t local_address)
{
    fake.reconfigured_address = local_address;
    return fake.can_configure_ok;
}

bool boot_can_abort_pending_tx(void)
{
    ++fake.can_abort_calls;
    return fake.can_abort_ok;
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
    assert(fake.tx_count < TX_CAPACITY);
    fake.tx[fake.tx_count++] = *frame;
    return BOOT_CAN_STATUS_OK;
}

boot_can_status_t boot_can_receive(boot_can_frame_t *frame)
{
    assert(frame != NULL);
    if (fake.rx_head == fake.rx_count)
    {
        return BOOT_CAN_STATUS_EMPTY;
    }
    *frame = fake.rx[fake.rx_head++];
    return BOOT_CAN_STATUS_OK;
}

boot_can_status_t boot_can_get_state(void)
{
    return BOOT_CAN_STATUS_OK;
}

bool boot_can_tx_idle(void)
{
    return fake.tx_idle;
}

boot_flash_status_t boot_flash_init(void)
{
    return BOOT_FLASH_STATUS_OK;
}

bool boot_flash_is_valid_range(boot_flash_region_t region,
                               uint32_t offset,
                               size_t length)
{
    (void)region;
    (void)offset;
    (void)length;
    return true;
}

boot_flash_status_t boot_flash_erase(boot_flash_region_t region,
                                     uint32_t offset,
                                     size_t length)
{
    assert(region == BOOT_FLASH_REGION_APPLICATION);
    ++fake.erase_calls;
    fake.erase_offset = offset;
    fake.erase_length = length;
    fake.now_ms += fake.erase_duration_ms;
    return BOOT_FLASH_STATUS_OK;
}

boot_flash_status_t boot_flash_write(boot_flash_region_t region,
                                     uint32_t offset,
                                     const uint8_t *data,
                                     size_t length)
{
    assert(region == BOOT_FLASH_REGION_APPLICATION);
    assert(data != NULL);
    assert(length <= sizeof(fake.write_data));
    ++fake.write_calls;
    fake.write_offset = offset;
    fake.write_length = length;
    memcpy(fake.write_data, data, length);
    return BOOT_FLASH_STATUS_OK;
}

boot_flash_status_t boot_flash_read(boot_flash_region_t region,
                                    uint32_t offset,
                                    uint8_t *data,
                                    size_t length)
{
    (void)region;
    (void)offset;
    (void)data;
    (void)length;
    return BOOT_FLASH_STATUS_HARDWARE;
}

void boot_device_config_defaults(boot_device_config_t *config)
{
    assert(config != NULL);
    *config = fake.config;
}

bool boot_device_config_read(boot_device_config_t *config)
{
    assert(config != NULL);
    *config = fake.config;
    return true;
}

void boot_device_config_load(boot_device_config_t *config)
{
    assert(config != NULL);
    *config = fake.config;
}

boot_flash_status_t boot_device_config_store(const boot_device_config_t *config)
{
    assert(config != NULL);
    fake.config = *config;
    return BOOT_FLASH_STATUS_OK;
}

boot_flash_status_t boot_device_config_store_preferred_address(uint8_t address)
{
    fake.config.preferred_address = address;
    ++fake.config.generation;
    return BOOT_FLASH_STATUS_OK;
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

boot_flash_status_t boot_metadata_begin(uint32_t image_size,
                                        uint32_t image_crc32)
{
    ++fake.metadata_begin_calls;
    fake.metadata_size = image_size;
    fake.metadata_crc = image_crc32;
    return BOOT_FLASH_STATUS_OK;
}

boot_flash_status_t boot_metadata_mark_valid(void)
{
    ++fake.metadata_mark_valid_calls;
    fake.image_valid = true;
    fake.metadata_read_ok = true;
    fake.metadata = (boot_metadata_t){
        .image_size = fake.metadata_size,
        .image_crc32 = fake.metadata_crc,
        .image_version = 0U,
        .state = BOOT_METADATA_STATE_VALID
    };
    return BOOT_FLASH_STATUS_OK;
}

boot_flash_status_t boot_metadata_mark_invalid(void)
{
    ++fake.metadata_mark_invalid_calls;
    fake.image_valid = false;
    return BOOT_FLASH_STATUS_OK;
}

bool boot_image_calculate_crc(uint32_t image_size, uint32_t *crc32)
{
    assert(image_size == fake.metadata_size);
    assert(crc32 != NULL);
    *crc32 = fake.calculated_crc;
    return fake.crc_ok;
}

bool boot_image_vectors_valid(uint32_t image_size,
                              uint32_t *initial_stack_pointer,
                              uint32_t *reset_handler)
{
    (void)image_size;
    assert(initial_stack_pointer != NULL);
    assert(reset_handler != NULL);
    *initial_stack_pointer = BOOT_SRAM_START_ADDRESS + 0x100U;
    *reset_handler = BOOT_APPLICATION_ADDRESS + 9U;
    return fake.vectors_valid;
}

bool boot_image_validate(boot_metadata_t *metadata)
{
    if (fake.image_valid && (metadata != NULL))
    {
        *metadata = fake.metadata;
    }
    return fake.image_valid;
}

void boot_image_jump(void)
{
    assert(false && "unexpected application jump");
}

uint32_t boot_platform_millis(void)
{
    return fake.now_ms;
}

void boot_platform_led_set(bool on)
{
    (void)on;
}

uint32_t boot_platform_uid_word(uint8_t index)
{
    return UINT32_C(0x11223344) + index;
}

bool boot_platform_take_boot_request(void)
{
    return fake.boot_request;
}

void boot_platform_set_boot_request(void)
{
    fake.boot_request = true;
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
    assert(false && "unexpected platform jump");
}

void boot_platform_reset(void)
{
    assert(false && "unexpected reset");
}

static boot_can_frame_t make_control(boot_command_t command,
                                     uint8_t source,
                                     uint8_t session,
                                     uint8_t parameter,
                                     uint32_t value)
{
    boot_can_frame_t frame;
    uint8_t data[8] = {
        BOOT_PROTOCOL_VERSION,
        (uint8_t)command,
        session,
        parameter,
        0U,
        0U,
        0U,
        0U
    };

    boot_protocol_write_u32_le(&data[4], value);
    assert(boot_j1939_make_frame(&frame,
                                 BOOT_J1939_PRIORITY,
                                 BOOT_J1939_PGN_PROPRIETARY_A,
                                 source,
                                 BOOT_J1939_DEFAULT_PREFERRED_ADDRESS,
                                 data,
                                 sizeof(data)));
    return frame;
}

static boot_can_frame_t make_dm14(boot_dm14_command_t command,
                                  uint8_t source,
                                  uint16_t length,
                                  uint32_t pointer)
{
    boot_can_frame_t frame;
    uint8_t data[8] = {0};

    data[0] = (uint8_t)length;
    data[1] = (uint8_t)((((length >> 8U) & 7U) << 5U) |
                        ((uint8_t)command << 1U) | 1U);
    boot_protocol_write_u24_le(&data[2], pointer);
    data[5] = (uint8_t)(pointer >> 24U);
    boot_protocol_write_u16_le(&data[6], UINT16_MAX);
    assert(boot_j1939_make_frame(&frame,
                                 BOOT_J1939_PRIORITY,
                                 BOOT_J1939_PGN_DM14,
                                 source,
                                 BOOT_J1939_DEFAULT_PREFERRED_ADDRESS,
                                 data,
                                 sizeof(data)));
    return frame;
}

static boot_can_frame_t make_tp_cm(uint8_t control,
                                   uint16_t size,
                                   uint8_t packets,
                                   uint8_t window)
{
    boot_can_frame_t frame;
    uint8_t data[8] = {control, 0U, 0U, packets, window, 0U, 0U, 0U};

    boot_protocol_write_u16_le(&data[1], size);
    boot_protocol_write_u24_le(&data[5], BOOT_J1939_PGN_DM16);
    assert(boot_j1939_make_frame(&frame,
                                 BOOT_J1939_PRIORITY,
                                 BOOT_J1939_PGN_TP_CM,
                                 HOST_ADDRESS,
                                 BOOT_J1939_DEFAULT_PREFERRED_ADDRESS,
                                 data,
                                 sizeof(data)));
    return frame;
}

static boot_can_frame_t make_tp_dt(uint8_t sequence, const uint8_t data[7])
{
    boot_can_frame_t frame;
    uint8_t payload[8];

    payload[0] = sequence;
    memcpy(&payload[1], data, 7U);
    assert(boot_j1939_make_frame(&frame,
                                 BOOT_J1939_PRIORITY,
                                 BOOT_J1939_PGN_TP_DT,
                                 HOST_ADDRESS,
                                 BOOT_J1939_DEFAULT_PREFERRED_ADDRESS,
                                 payload,
                                 sizeof(payload)));
    return frame;
}

static boot_can_frame_t make_bam(uint8_t source,
                                 uint32_t transported_pgn,
                                 uint16_t size)
{
    boot_can_frame_t frame;
    uint8_t data[8] = {
        BOOT_J1939_TP_CONTROL_BAM,
        0U,
        0U,
        (uint8_t)((size + 6U) / 7U),
        UINT8_C(0xFF),
        0U,
        0U,
        0U
    };

    boot_protocol_write_u16_le(&data[1], size);
    boot_protocol_write_u24_le(&data[5], transported_pgn);
    assert(boot_j1939_make_frame(&frame,
                                 BOOT_J1939_PRIORITY,
                                 BOOT_J1939_PGN_TP_CM,
                                 source,
                                 BOOT_J1939_GLOBAL_ADDRESS,
                                 data,
                                 sizeof(data)));
    return frame;
}

static boot_can_frame_t make_address_claim(uint8_t source, uint64_t name)
{
    boot_can_frame_t frame;
    uint8_t data[8];

    boot_protocol_write_u64_le(data, name);
    assert(boot_j1939_make_frame(&frame,
                                 BOOT_J1939_PRIORITY,
                                 BOOT_J1939_PGN_ADDRESS_CLAIMED,
                                 source,
                                 BOOT_J1939_GLOBAL_ADDRESS,
                                 data,
                                 sizeof(data)));
    return frame;
}

static void start_and_claim(void)
{
    boot_j1939_header_t header;

    bootloader_init();
    assert(bootloader_get_state() == BOOTLOADER_STATE_WAIT_HOST);
    assert(fake.configured_address == BOOT_J1939_DEFAULT_PREFERRED_ADDRESS);
    bootloader_task();
    assert(fake.tx_count == 1U);
    assert(boot_j1939_decode_frame(&fake.tx[0], &header));
    assert(header.pgn == BOOT_J1939_PGN_ADDRESS_CLAIMED);
    assert(header.source_address == BOOT_J1939_DEFAULT_PREFERRED_ADDRESS);
    assert(boot_protocol_read_u64_le(fake.tx[0].data) == TARGET_NAME);
    fake.now_ms = BOOT_J1939_ADDRESS_CLAIM_MS;
    bootloader_task();
}

static size_t exchange(const boot_can_frame_t *frame)
{
    const size_t first = fake.tx_count;

    enqueue(frame);
    bootloader_task();
    return first;
}

static uint32_t assert_control_response_to(size_t index,
                                           uint8_t destination,
                                           boot_response_kind_t kind,
                                           boot_command_t command,
                                           uint8_t session)
{
    boot_j1939_header_t header;
    const boot_can_frame_t *frame = &fake.tx[index];

    assert(index < fake.tx_count);
    assert(boot_j1939_decode_frame(frame, &header));
    assert(header.pgn == BOOT_J1939_PGN_PROPRIETARY_A);
    assert(header.source_address == BOOT_J1939_DEFAULT_PREFERRED_ADDRESS);
    assert(header.destination_address == destination);
    assert(frame->data[0] == BOOT_PROTOCOL_VERSION);
    assert(frame->data[1] == (uint8_t)kind);
    assert(frame->data[2] == (uint8_t)command);
    assert(frame->data[3] == session);
    return boot_protocol_read_u32_le(&frame->data[4]);
}

static uint32_t assert_control_response(size_t index,
                                        boot_response_kind_t kind,
                                        boot_command_t command,
                                        uint8_t session)
{
    return assert_control_response_to(index,
                                      HOST_ADDRESS,
                                      kind,
                                      command,
                                      session);
}

static void begin_update(uint32_t image_size,
                         uint32_t crc,
                         uint8_t session)
{
    boot_can_frame_t frame;
    size_t first;

    frame = make_control(BOOT_COMMAND_START_UPDATE,
                         HOST_ADDRESS,
                         session,
                         0U,
                         image_size);
    first = exchange(&frame);
    assert(assert_control_response(first,
                                   BOOT_RESPONSE_ACK,
                                   BOOT_COMMAND_START_UPDATE,
                                   session) == BOOT_TRANSFER_BLOCK_SIZE);

    frame = make_control(BOOT_COMMAND_SET_IMAGE_CRC,
                         HOST_ADDRESS,
                         session,
                         0U,
                         crc);
    first = exchange(&frame);
    assert(assert_control_response(first,
                                   BOOT_RESPONSE_ACK,
                                   BOOT_COMMAND_SET_IMAGE_CRC,
                                   session) == crc);
}

static boot_dm15_status_t assert_dm15(size_t index,
                                      uint8_t destination,
                                      uint16_t length)
{
    boot_j1939_header_t header;
    const boot_can_frame_t *frame = &fake.tx[index];

    assert(index < fake.tx_count);
    assert(boot_j1939_decode_frame(frame, &header));
    assert(header.pgn == BOOT_J1939_PGN_DM15);
    assert(header.destination_address == destination);
    assert((uint16_t)((uint16_t)frame->data[0] |
                      ((uint16_t)(frame->data[1] >> 5U) << 8U)) == length);
    return (boot_dm15_status_t)((frame->data[1] >> 1U) & 7U);
}

static void test_claim_identify_and_info(void)
{
    boot_can_frame_t frame;
    size_t first;
    uint32_t capabilities;

    fake_reset();
    start_and_claim();

    frame = make_control(BOOT_COMMAND_IDENTIFY, HOST_ADDRESS, 0U, 0U, 0U);
    first = exchange(&frame);
    assert(assert_control_response(first,
                                   BOOT_RESPONSE_NACK,
                                   BOOT_COMMAND_IDENTIFY,
                                   0U) == BOOT_ERROR_SESSION);

    frame = make_control(BOOT_COMMAND_IDENTIFY, HOST_ADDRESS, 7U, 0U, 0U);
    first = exchange(&frame);
    capabilities = assert_control_response(first,
                                            BOOT_RESPONSE_ACK,
                                            BOOT_COMMAND_IDENTIFY,
                                            7U);
    assert(((capabilities >> 8U) & 0xFFU) ==
           BOOT_J1939_DEFAULT_PREFERRED_ADDRESS);
    assert(((capabilities >> 16U) & 0xFFU) == BOOT_PROTOCOL_VERSION);
    assert((capabilities & BOOT_CAPABILITY_J1939) != 0U);

    frame = make_control(BOOT_COMMAND_INFO,
                         HOST_ADDRESS,
                         8U,
                         BOOT_INFO_APPLICATION_BASE,
                         0U);
    first = exchange(&frame);
    assert(assert_control_response(first,
                                   BOOT_RESPONSE_ACK,
                                   BOOT_COMMAND_INFO,
                                   8U) == BOOT_APPLICATION_ADDRESS);
}

static void test_update_owner_and_complete_tp_block(void)
{
    static const uint8_t firmware[8] = {
        0x00U, 0x01U, 0x00U, 0x20U, 0x09U, 0x50U, 0x00U, 0x08U
    };
    const uint8_t first_dt[7] = {
        UINT8_C(0xFF), firmware[0], firmware[1], firmware[2],
        firmware[3], firmware[4], firmware[5]
    };
    const uint8_t second_dt[7] = {
        firmware[6], firmware[7], UINT8_C(0xFF), UINT8_C(0xFF),
        UINT8_C(0xFF), UINT8_C(0xFF), UINT8_C(0xFF)
    };
    const uint8_t session = 21U;
    const uint32_t crc = UINT32_C(0x89ABCDEF);
    boot_can_frame_t frame;
    boot_j1939_header_t header;
    size_t first;

    fake_reset();
    fake.calculated_crc = crc;
    start_and_claim();

    frame = make_control(BOOT_COMMAND_START_UPDATE,
                         HOST_ADDRESS,
                         session,
                         0U,
                         sizeof(firmware));
    first = exchange(&frame);
    assert(assert_control_response(first,
                                   BOOT_RESPONSE_ACK,
                                   BOOT_COMMAND_START_UPDATE,
                                   session) == BOOT_TRANSFER_BLOCK_SIZE);

    frame = make_control(BOOT_COMMAND_SET_IMAGE_CRC,
                         HOST_ADDRESS,
                         session,
                         0U,
                         crc);
    first = exchange(&frame);
    assert(assert_control_response(first,
                                   BOOT_RESPONSE_ACK,
                                   BOOT_COMMAND_SET_IMAGE_CRC,
                                   session) == crc);

    frame = make_dm14(BOOT_DM14_COMMAND_ERASE,
                      OTHER_HOST_ADDRESS,
                      1U,
                      BOOT_APPLICATION_ADDRESS);
    first = exchange(&frame);
    assert(assert_dm15(first, OTHER_HOST_ADDRESS, 1U) ==
           BOOT_DM15_STATUS_OPERATION_FAILED);
    assert(fake.erase_calls == 0U);

    frame = make_dm14(BOOT_DM14_COMMAND_ERASE,
                      HOST_ADDRESS,
                      1U,
                      BOOT_APPLICATION_ADDRESS);
    first = exchange(&frame);
    assert(fake.tx_count == first + 2U);
    assert(assert_dm15(first, HOST_ADDRESS, 1U) == BOOT_DM15_STATUS_PROCEED);
    assert(assert_dm15(first + 1U, HOST_ADDRESS, 1U) ==
           BOOT_DM15_STATUS_OPERATION_COMPLETED);
    assert(fake.metadata_begin_calls == 1U);
    assert(fake.erase_calls == 1U);
    assert(fake.erase_offset == 0U);
    assert(fake.erase_length == BOOT_FLASH_PAGE_SIZE);

    frame = make_dm14(BOOT_DM14_COMMAND_OPERATION_COMPLETED,
                      HOST_ADDRESS,
                      1U,
                      BOOT_APPLICATION_ADDRESS);
    (void)exchange(&frame);

    frame = make_dm14(BOOT_DM14_COMMAND_WRITE,
                      HOST_ADDRESS,
                      sizeof(firmware),
                      BOOT_APPLICATION_ADDRESS);
    first = exchange(&frame);
    assert(assert_dm15(first, HOST_ADDRESS, sizeof(firmware)) ==
           BOOT_DM15_STATUS_PROCEED);

    /* A broadcast transfer owned by another ECU must not replace the
     * already-authorized destination-specific DM16 expectation. */
    frame = make_bam(OTHER_HOST_ADDRESS,
                     BOOT_J1939_PGN_COMMANDED_ADDRESS,
                     9U);
    first = exchange(&frame);
    assert(fake.tx_count == first);

    frame = make_tp_cm(BOOT_J1939_TP_CONTROL_RTS, 9U, 2U, 2U);
    first = exchange(&frame);
    assert(boot_j1939_decode_frame(&fake.tx[first], &header));
    assert(header.pgn == BOOT_J1939_PGN_TP_CM);
    assert(fake.tx[first].data[0] == BOOT_J1939_TP_CONTROL_CTS);

    frame = make_tp_dt(1U, first_dt);
    first = exchange(&frame);
    assert(fake.tx_count == first);

    frame = make_tp_dt(2U, second_dt);
    first = exchange(&frame);
    assert(fake.tx_count == first + 2U);
    assert(boot_j1939_decode_frame(&fake.tx[first], &header));
    assert(header.pgn == BOOT_J1939_PGN_TP_CM);
    assert(fake.tx[first].data[0] == BOOT_J1939_TP_CONTROL_EOM_ACK);
    assert(assert_dm15(first + 1U, HOST_ADDRESS, sizeof(firmware)) ==
           BOOT_DM15_STATUS_OPERATION_COMPLETED);
    assert(fake.write_calls == 1U);
    assert(fake.write_offset == 0U);
    assert(fake.write_length == sizeof(firmware));
    assert(memcmp(fake.write_data, firmware, sizeof(firmware)) == 0);

    frame = make_dm14(BOOT_DM14_COMMAND_OPERATION_COMPLETED,
                      HOST_ADDRESS,
                      sizeof(firmware),
                      BOOT_APPLICATION_ADDRESS);
    first = exchange(&frame);
    assert(fake.tx_count == first);
    first = exchange(&frame);
    assert(fake.tx_count == first);

    frame = make_control(BOOT_COMMAND_FINISH,
                         HOST_ADDRESS,
                         session,
                         0U,
                         crc);
    first = exchange(&frame);
    assert(assert_control_response(first,
                                   BOOT_RESPONSE_ACK,
                                   BOOT_COMMAND_FINISH,
                                   session) == crc);
    assert(fake.metadata_mark_valid_calls == 1U);
    assert(bootloader_get_state() == BOOTLOADER_STATE_READY);

    frame = make_control(BOOT_COMMAND_ABORT,
                         OTHER_HOST_ADDRESS,
                         99U,
                         0U,
                         0U);
    first = exchange(&frame);
    assert(assert_control_response_to(first,
                                      OTHER_HOST_ADDRESS,
                                      BOOT_RESPONSE_ACK,
                                      BOOT_COMMAND_ABORT,
                                      99U) == 0U);
    assert(fake.metadata_mark_invalid_calls == 0U);
    assert(fake.image_valid);
    assert(bootloader_get_state() == BOOTLOADER_STATE_READY);
}

static void test_active_update_guards_and_foreign_timeout(void)
{
    static const boot_command_t guarded_commands[] = {
        BOOT_COMMAND_VERIFY,
        BOOT_COMMAND_RUN_APPLICATION,
        BOOT_COMMAND_ENTER_BOOTLOADER
    };
    const uint8_t session = 37U;
    const uint32_t activity_started = BOOT_J1939_ADDRESS_CLAIM_MS;
    boot_can_frame_t frame;
    size_t first;
    size_t index;

    fake_reset();
    start_and_claim();
    begin_update(8U, UINT32_C(0x10203040), session);

    for (index = 0U;
         index < (sizeof(guarded_commands) / sizeof(guarded_commands[0]));
         ++index)
    {
        frame = make_control(guarded_commands[index],
                             HOST_ADDRESS,
                             session,
                             0U,
                             0U);
        first = exchange(&frame);
        assert(assert_control_response(first,
                                       BOOT_RESPONSE_NACK,
                                       guarded_commands[index],
                                       session) == BOOT_ERROR_BUSY);
        assert(bootloader_get_state() == BOOTLOADER_STATE_PREPARE_UPDATE);

        frame = make_control(guarded_commands[index],
                             OTHER_HOST_ADDRESS,
                             session,
                             0U,
                             0U);
        first = exchange(&frame);
        assert(assert_control_response_to(first,
                                          OTHER_HOST_ADDRESS,
                                          BOOT_RESPONSE_NACK,
                                          guarded_commands[index],
                                          session) == BOOT_ERROR_SESSION);
        assert(bootloader_get_state() == BOOTLOADER_STATE_PREPARE_UPDATE);
    }

    fake.now_ms = activity_started + BOOT_TRANSFER_TIMEOUT_MS - 1U;
    frame = make_control(BOOT_COMMAND_IDENTIFY,
                         OTHER_HOST_ADDRESS,
                         88U,
                         0U,
                         0U);
    first = exchange(&frame);
    (void)assert_control_response_to(first,
                                     OTHER_HOST_ADDRESS,
                                     BOOT_RESPONSE_ACK,
                                     BOOT_COMMAND_IDENTIFY,
                                     88U);
    assert(bootloader_get_state() == BOOTLOADER_STATE_PREPARE_UPDATE);

    frame = make_control(BOOT_COMMAND_IDENTIFY,
                         HOST_ADDRESS,
                         session,
                         0U,
                         0U);
    first = exchange(&frame);
    (void)assert_control_response(first,
                                  BOOT_RESPONSE_ACK,
                                  BOOT_COMMAND_IDENTIFY,
                                  session);
    assert(bootloader_get_state() == BOOTLOADER_STATE_PREPARE_UPDATE);

    fake.now_ms = activity_started + BOOT_TRANSFER_TIMEOUT_MS;
    first = fake.tx_count;
    bootloader_task();
    assert(assert_control_response(first,
                                   BOOT_RESPONSE_NACK,
                                   BOOT_COMMAND_ABORT,
                                   session) == BOOT_ERROR_TIMEOUT);
    assert(bootloader_get_state() == BOOTLOADER_STATE_WAIT_HOST);
}

static void test_dm14_failed_erase_returns_to_prepare(void)
{
    const uint8_t session = 41U;
    boot_can_frame_t frame;
    size_t first;

    fake_reset();
    start_and_claim();
    begin_update(8U, UINT32_C(0x55667788), session);

    fake.tx_idle = false;
    frame = make_dm14(BOOT_DM14_COMMAND_ERASE,
                      HOST_ADDRESS,
                      1U,
                      BOOT_APPLICATION_ADDRESS);
    first = exchange(&frame);
    assert(assert_dm15(first, HOST_ADDRESS, 1U) == BOOT_DM15_STATUS_PROCEED);
    assert(bootloader_get_state() == BOOTLOADER_STATE_ERASE);
    assert(fake.erase_calls == 0U);

    frame = make_dm14(BOOT_DM14_COMMAND_OPERATION_FAILED,
                      HOST_ADDRESS,
                      1U,
                      BOOT_APPLICATION_ADDRESS);
    first = exchange(&frame);
    assert(fake.tx_count == first);
    assert(bootloader_get_state() == BOOTLOADER_STATE_PREPARE_UPDATE);

    fake.tx_idle = true;
    frame = make_dm14(BOOT_DM14_COMMAND_ERASE,
                      HOST_ADDRESS,
                      1U,
                      BOOT_APPLICATION_ADDRESS);
    first = exchange(&frame);
    assert(assert_dm15(first, HOST_ADDRESS, 1U) == BOOT_DM15_STATUS_PROCEED);
    assert(assert_dm15(first + 1U, HOST_ADDRESS, 1U) ==
           BOOT_DM15_STATUS_OPERATION_COMPLETED);
    assert(fake.erase_calls == 1U);
}

static void test_synchronous_erase_uses_fresh_timeout_tick(void)
{
    boot_can_frame_t frame;
    size_t first;

    fake_reset();
    start_and_claim();
    begin_update(8U, UINT32_C(0xA1B2C3D4), 42U);
    fake.erase_duration_ms = 25U;

    frame = make_dm14(BOOT_DM14_COMMAND_ERASE,
                      HOST_ADDRESS,
                      1U,
                      BOOT_APPLICATION_ADDRESS);
    first = exchange(&frame);
    assert(assert_dm15(first, HOST_ADDRESS, 1U) == BOOT_DM15_STATUS_PROCEED);
    assert(assert_dm15(first + 1U, HOST_ADDRESS, 1U) ==
           BOOT_DM15_STATUS_OPERATION_COMPLETED);
    assert(bootloader_get_state() == BOOTLOADER_STATE_RECEIVE);
    assert(fake.metadata_mark_invalid_calls == 0U);
}

static void test_address_conflict_reconfigures_or_fails_closed(void)
{
    boot_can_frame_t frame;
    boot_j1939_header_t header;
    size_t first;

    fake_reset();
    start_and_claim();
    begin_update(8U, UINT32_C(0xCAFEBABE), 43U);
    frame = make_address_claim(BOOT_J1939_DEFAULT_PREFERRED_ADDRESS,
                               TARGET_NAME - 1U);
    first = exchange(&frame);
    assert(bootloader_get_state() == BOOTLOADER_STATE_WAIT_HOST);
    assert(fake.reconfigured_address ==
           (uint8_t)(BOOT_J1939_DEFAULT_PREFERRED_ADDRESS + 1U));
    assert(fake.can_abort_calls == 1U);
    assert(fake.tx_count == first + 1U);
    assert(boot_j1939_decode_frame(&fake.tx[first], &header));
    assert(header.pgn == BOOT_J1939_PGN_ADDRESS_CLAIMED);
    assert(header.source_address ==
           (uint8_t)(BOOT_J1939_DEFAULT_PREFERRED_ADDRESS + 1U));
    assert(boot_protocol_read_u64_le(fake.tx[first].data) == TARGET_NAME);

    fake_reset();
    start_and_claim();
    fake.can_abort_ok = false;
    frame = make_address_claim(BOOT_J1939_DEFAULT_PREFERRED_ADDRESS,
                               TARGET_NAME - 1U);
    (void)exchange(&frame);
    assert(bootloader_get_state() == BOOTLOADER_STATE_ERROR);
    assert(fake.can_abort_calls == 1U);
    assert(fake.reconfigured_address == 0U);

    fake_reset();
    start_and_claim();
    fake.can_configure_ok = false;
    frame = make_address_claim(BOOT_J1939_DEFAULT_PREFERRED_ADDRESS,
                               TARGET_NAME - 1U);
    (void)exchange(&frame);
    assert(bootloader_get_state() == BOOTLOADER_STATE_ERROR);
}

static void test_slow_tp_renews_update_activity(void)
{
    uint8_t firmware[BOOT_TRANSFER_BLOCK_SIZE];
    uint8_t wire[BOOT_TRANSFER_BLOCK_SIZE + 1U];
    uint8_t chunk[7];
    boot_can_frame_t frame;
    boot_j1939_header_t header;
    size_t first;
    uint16_t index;
    uint8_t sequence;

    for (index = 0U; index < sizeof(firmware); ++index)
    {
        firmware[index] = (uint8_t)index;
    }
    wire[0] = UINT8_C(0xFF);
    memcpy(&wire[1], firmware, sizeof(firmware));

    fake_reset();
    start_and_claim();
    begin_update(sizeof(firmware), UINT32_C(0x01020304), 47U);
    frame = make_dm14(BOOT_DM14_COMMAND_ERASE,
                      HOST_ADDRESS,
                      1U,
                      BOOT_APPLICATION_ADDRESS);
    (void)exchange(&frame);
    frame = make_dm14(BOOT_DM14_COMMAND_OPERATION_COMPLETED,
                      HOST_ADDRESS,
                      1U,
                      BOOT_APPLICATION_ADDRESS);
    (void)exchange(&frame);
    frame = make_dm14(BOOT_DM14_COMMAND_WRITE,
                      HOST_ADDRESS,
                      sizeof(firmware),
                      BOOT_APPLICATION_ADDRESS);
    (void)exchange(&frame);

    frame = make_tp_cm(BOOT_J1939_TP_CONTROL_RTS,
                       sizeof(wire),
                       15U,
                       15U);
    first = exchange(&frame);
    assert(boot_j1939_decode_frame(&fake.tx[first], &header));
    assert(header.pgn == BOOT_J1939_PGN_TP_CM);
    assert(fake.tx[first].data[0] == BOOT_J1939_TP_CONTROL_CTS);

    for (sequence = 1U; sequence <= 15U; ++sequence)
    {
        const uint16_t offset = (uint16_t)(sequence - 1U) * 7U;
        const uint16_t remaining = (uint16_t)sizeof(wire) - offset;
        const uint8_t amount = (remaining > 7U) ? 7U : (uint8_t)remaining;

        memset(chunk, UINT8_MAX, sizeof(chunk));
        memcpy(chunk, &wire[offset], amount);
        fake.now_ms += 1000U;
        frame = make_tp_dt(sequence, chunk);
        first = exchange(&frame);
        if (sequence == 8U)
        {
            assert(fake.tx_count == first + 1U);
            assert(fake.tx[first].data[0] == BOOT_J1939_TP_CONTROL_CTS);
        }
        else if (sequence == 15U)
        {
            assert(fake.tx_count == first + 2U);
            assert(fake.tx[first].data[0] == BOOT_J1939_TP_CONTROL_EOM_ACK);
            assert(assert_dm15(first + 1U,
                               HOST_ADDRESS,
                               sizeof(firmware)) ==
                   BOOT_DM15_STATUS_OPERATION_COMPLETED);
        }
        assert(bootloader_get_state() == BOOTLOADER_STATE_RECEIVE);
    }
    assert(fake.write_calls == 1U);
    assert(memcmp(fake.write_data, firmware, sizeof(firmware)) == 0);
}

static void test_standard_frame_is_ignored(void)
{
    boot_can_frame_t frame = {0};
    size_t before;

    fake_reset();
    start_and_claim();
    frame.id = UINT32_C(0x601);
    frame.id_type = BOOT_CAN_ID_STANDARD;
    frame.dlc = 8U;
    before = fake.tx_count;
    (void)exchange(&frame);
    assert(fake.tx_count == before);
}

int main(void)
{
    test_claim_identify_and_info();
    test_update_owner_and_complete_tp_block();
    test_active_update_guards_and_foreign_timeout();
    test_dm14_failed_erase_returns_to_prepare();
    test_synchronous_erase_uses_fresh_timeout_tick();
    test_address_conflict_reconfigures_or_fails_closed();
    test_slow_tp_renews_update_activity();
    test_standard_frame_is_ignored();
    puts("bootloader v2 tests: OK");
    return 0;
}
