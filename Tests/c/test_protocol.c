#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "boot_config.h"
#include "boot_crc32.h"
#include "boot_protocol.h"

#define TEST_NODE_ID BOOT_DEFAULT_NODE_ID

static void test_crc32_known_vector(void)
{
    static const uint8_t input[] = "123456789";
    uint32_t crc;

    assert(boot_crc32_compute(input, sizeof(input) - 1U) ==
           UINT32_C(0xCBF43926));

    crc = boot_crc32_update(BOOT_CRC32_INITIAL_VALUE, input, 4U);
    crc = boot_crc32_update(crc, &input[4], (sizeof(input) - 1U) - 4U);
    assert(boot_crc32_finalize(crc) == UINT32_C(0xCBF43926));

    assert(boot_crc32_compute(NULL, 0U) == UINT32_C(0x00000000));
    assert(boot_crc32_update(BOOT_CRC32_INITIAL_VALUE, NULL, 1U) ==
           BOOT_CRC32_INITIAL_VALUE);
}

static void test_little_endian_helpers(void)
{
    uint8_t bytes[4] = {0U};

    boot_protocol_write_u16_le(bytes, UINT16_C(0xA1B2));
    assert(bytes[0] == UINT8_C(0xB2));
    assert(bytes[1] == UINT8_C(0xA1));
    assert(boot_protocol_read_u16_le(bytes) == UINT16_C(0xA1B2));

    boot_protocol_write_u32_le(bytes, UINT32_C(0x89ABCDEF));
    assert(bytes[0] == UINT8_C(0xEF));
    assert(bytes[1] == UINT8_C(0xCD));
    assert(bytes[2] == UINT8_C(0xAB));
    assert(bytes[3] == UINT8_C(0x89));
    assert(boot_protocol_read_u32_le(bytes) == UINT32_C(0x89ABCDEF));
}

static boot_can_frame_t valid_request_frame(void)
{
    boot_can_frame_t frame = {0};

    frame.id = BOOT_CAN_COMMAND_ID(TEST_NODE_ID);
    frame.dlc = BOOT_CAN_MAX_DATA_LENGTH;
    frame.data[0] = BOOT_PROTOCOL_VERSION;
    frame.data[1] = (uint8_t)BOOT_COMMAND_START_UPDATE;
    frame.data[2] = UINT8_C(0x5A);
    frame.data[3] = UINT8_C(0xC3);
    boot_protocol_write_u32_le(&frame.data[4], UINT32_C(0x12345678));
    return frame;
}

static void test_decode_control_request(void)
{
    boot_can_frame_t frame = valid_request_frame();
    boot_protocol_request_t request = {0};
    boot_error_t error = BOOT_ERROR_INTERNAL;

    assert(boot_protocol_decode_request(&frame, TEST_NODE_ID, &request, &error));
    assert(error == BOOT_ERROR_NONE);
    assert(request.version == BOOT_PROTOCOL_VERSION);
    assert(request.command == BOOT_COMMAND_START_UPDATE);
    assert(request.session == UINT8_C(0x5A));
    assert(request.parameter == UINT8_C(0xC3));
    assert(request.value == UINT32_C(0x12345678));

    frame.id = BOOT_CAN_BROADCAST_ID;
    assert(boot_protocol_decode_request(&frame, TEST_NODE_ID, &request, &error));
    assert(error == BOOT_ERROR_NONE);
}

static void test_rejects_malformed_control_frames(void)
{
    boot_can_frame_t frame = valid_request_frame();
    boot_protocol_request_t request = {0};
    boot_error_t error = BOOT_ERROR_NONE;

    frame.id = BOOT_CAN_DATA_ID(TEST_NODE_ID);
    assert(!boot_protocol_decode_request(&frame, TEST_NODE_ID, &request, &error));
    assert(error == BOOT_ERROR_MALFORMED_FRAME);

    frame = valid_request_frame();
    frame.dlc = BOOT_CAN_MAX_DATA_LENGTH - 1U;
    assert(!boot_protocol_decode_request(&frame, TEST_NODE_ID, &request, &error));
    assert(error == BOOT_ERROR_MALFORMED_FRAME);

    frame = valid_request_frame();
    frame.data[0] = (uint8_t)(BOOT_PROTOCOL_VERSION + 1U);
    assert(!boot_protocol_decode_request(&frame, TEST_NODE_ID, &request, &error));
    assert(error == BOOT_ERROR_UNSUPPORTED_VERSION);

    assert(!boot_protocol_decode_request(NULL, TEST_NODE_ID, &request, &error));
    assert(!boot_protocol_decode_request(&frame, TEST_NODE_ID, NULL, &error));
    assert(!boot_protocol_decode_request(&frame, TEST_NODE_ID, &request, NULL));
}

static boot_protocol_request_t valid_block_request(uint8_t length)
{
    boot_protocol_request_t request = {0};

    request.version = BOOT_PROTOCOL_VERSION;
    request.command = BOOT_COMMAND_BEGIN_BLOCK;
    request.session = UINT8_C(0x41);
    request.parameter = UINT8_C(0x7E);
    request.value = UINT32_C(0x3456) | ((uint32_t)length << 16U);
    return request;
}

static void test_decode_block(void)
{
    boot_protocol_request_t request = valid_block_request(100U);
    boot_protocol_block_t block = {0};
    boot_error_t error = BOOT_ERROR_INTERNAL;

    assert(boot_protocol_decode_block(&request, &block, &error));
    assert(error == BOOT_ERROR_NONE);
    assert(block.block_sequence == UINT8_C(0x7E));
    assert(block.block_index == UINT16_C(0x3456));
    assert(block.block_length == UINT8_C(100));
}

static void test_rejects_invalid_blocks(void)
{
    boot_protocol_request_t request = valid_block_request(1U);
    boot_protocol_block_t block = {0};
    boot_error_t error = BOOT_ERROR_NONE;

    request.command = BOOT_COMMAND_INFO;
    assert(!boot_protocol_decode_block(&request, &block, &error));
    assert(error == BOOT_ERROR_MALFORMED_FRAME);

    request = valid_block_request(1U);
    request.value |= UINT32_C(0x01000000);
    assert(!boot_protocol_decode_block(&request, &block, &error));
    assert(error == BOOT_ERROR_MALFORMED_FRAME);

    request = valid_block_request(0U);
    assert(!boot_protocol_decode_block(&request, &block, &error));
    assert(error == BOOT_ERROR_INVALID_SIZE);

    request = valid_block_request((uint8_t)(BOOT_TRANSFER_BLOCK_SIZE + 1U));
    assert(!boot_protocol_decode_block(&request, &block, &error));
    assert(error == BOOT_ERROR_INVALID_SIZE);

    assert(!boot_protocol_decode_block(NULL, &block, &error));
    assert(!boot_protocol_decode_block(&request, NULL, &error));
    assert(!boot_protocol_decode_block(&request, &block, NULL));
}

static boot_can_frame_t valid_data_frame(void)
{
    boot_can_frame_t frame = {0};

    frame.id = BOOT_CAN_DATA_ID(TEST_NODE_ID);
    frame.dlc = BOOT_CAN_MAX_DATA_LENGTH;
    frame.data[0] = UINT8_C(0x51);
    frame.data[1] = UINT8_C(0x62);
    frame.data[2] = UINT8_C(0x73);
    frame.data[3] = UINT8_C(0x10);
    frame.data[4] = UINT8_C(0x20);
    frame.data[5] = UINT8_C(0x30);
    frame.data[6] = UINT8_C(0x40);
    frame.data[7] = UINT8_C(0x50);
    return frame;
}

static void test_decode_data(void)
{
    boot_can_frame_t frame = valid_data_frame();
    boot_protocol_data_t data = {0};
    boot_error_t error = BOOT_ERROR_INTERNAL;
    static const uint8_t expected[] = {0x10U, 0x20U, 0x30U, 0x40U, 0x50U};

    assert(boot_protocol_decode_data(&frame, TEST_NODE_ID, &data, &error));
    assert(error == BOOT_ERROR_NONE);
    assert(data.session == UINT8_C(0x51));
    assert(data.block_sequence == UINT8_C(0x62));
    assert(data.frame_sequence == UINT8_C(0x73));
    assert(data.payload_length == BOOT_DATA_BYTES_PER_FRAME);
    assert(memcmp(data.payload, expected, sizeof(expected)) == 0);

    frame.dlc = 4U;
    memset(&data, 0, sizeof(data));
    assert(boot_protocol_decode_data(&frame, TEST_NODE_ID, &data, &error));
    assert(data.payload_length == 1U);
    assert(data.payload[0] == UINT8_C(0x10));
    assert(data.payload[1] == UINT8_C(0xFF));
    assert(data.payload[2] == UINT8_C(0xFF));
    assert(data.payload[3] == UINT8_C(0xFF));
    assert(data.payload[4] == UINT8_C(0xFF));
}

static void test_rejects_malformed_data_frames(void)
{
    boot_can_frame_t frame = valid_data_frame();
    boot_protocol_data_t data = {0};
    boot_error_t error = BOOT_ERROR_NONE;

    frame.id = BOOT_CAN_COMMAND_ID(TEST_NODE_ID);
    assert(!boot_protocol_decode_data(&frame, TEST_NODE_ID, &data, &error));
    assert(error == BOOT_ERROR_MALFORMED_FRAME);

    frame = valid_data_frame();
    frame.dlc = 3U;
    assert(!boot_protocol_decode_data(&frame, TEST_NODE_ID, &data, &error));
    assert(error == BOOT_ERROR_MALFORMED_FRAME);

    frame = valid_data_frame();
    frame.dlc = BOOT_CAN_MAX_DATA_LENGTH + 1U;
    assert(!boot_protocol_decode_data(&frame, TEST_NODE_ID, &data, &error));
    assert(error == BOOT_ERROR_MALFORMED_FRAME);

    assert(!boot_protocol_decode_data(NULL, TEST_NODE_ID, &data, &error));
    assert(!boot_protocol_decode_data(&frame, TEST_NODE_ID, NULL, &error));
    assert(!boot_protocol_decode_data(&frame, TEST_NODE_ID, &data, NULL));
}

static void test_make_response(void)
{
    boot_can_frame_t frame = {0};

    boot_protocol_make_response(&frame,
                                TEST_NODE_ID,
                                BOOT_RESPONSE_NACK,
                                BOOT_COMMAND_ERASE,
                                UINT8_C(0xA5),
                                UINT32_C(0x78563412));

    assert(frame.id == BOOT_CAN_RESPONSE_ID(TEST_NODE_ID));
    assert(frame.dlc == BOOT_CAN_MAX_DATA_LENGTH);
    assert(frame.data[0] == BOOT_PROTOCOL_VERSION);
    assert(frame.data[1] == (uint8_t)BOOT_RESPONSE_NACK);
    assert(frame.data[2] == (uint8_t)BOOT_COMMAND_ERASE);
    assert(frame.data[3] == UINT8_C(0xA5));
    assert(frame.data[4] == UINT8_C(0x12));
    assert(frame.data[5] == UINT8_C(0x34));
    assert(frame.data[6] == UINT8_C(0x56));
    assert(frame.data[7] == UINT8_C(0x78));

    boot_protocol_make_response(NULL,
                                TEST_NODE_ID,
                                BOOT_RESPONSE_ACK,
                                BOOT_COMMAND_INFO,
                                0U,
                                0U);
}

int main(void)
{
    test_crc32_known_vector();
    test_little_endian_helpers();
    test_decode_control_request();
    test_rejects_malformed_control_frames();
    test_decode_block();
    test_rejects_invalid_blocks();
    test_decode_data();
    test_rejects_malformed_data_frames();
    test_make_response();

    puts("test_protocol: OK");
    return 0;
}
