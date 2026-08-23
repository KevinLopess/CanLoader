#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "boot_config.h"
#include "boot_crc32.h"
#include "boot_j1939.h"
#include "boot_protocol.h"

#define TEST_DEVICE_ADDRESS UINT8_C(0x80)
#define TEST_TOOL_ADDRESS   UINT8_C(0x90)

static void test_crc32_known_vector(void)
{
    static const uint8_t input[] = "123456789";

    assert(boot_crc32_compute(input, sizeof(input) - 1U) ==
           UINT32_C(0xCBF43926));
}

static void test_little_endian_helpers(void)
{
    uint8_t bytes[8] = {0U};

    boot_protocol_write_u16_le(bytes, UINT16_C(0xA1B2));
    assert(boot_protocol_read_u16_le(bytes) == UINT16_C(0xA1B2));
    boot_protocol_write_u24_le(bytes, UINT32_C(0x00C3D4E5));
    assert(boot_protocol_read_u24_le(bytes) == UINT32_C(0x00C3D4E5));
    boot_protocol_write_u32_le(bytes, UINT32_C(0x89ABCDEF));
    assert(boot_protocol_read_u32_le(bytes) == UINT32_C(0x89ABCDEF));
    boot_protocol_write_u64_le(bytes, UINT64_C(0x0123456789ABCDEF));
    assert(boot_protocol_read_u64_le(bytes) == UINT64_C(0x0123456789ABCDEF));
}

static void test_j1939_identifier_mapping(void)
{
    uint32_t identifier;
    boot_j1939_header_t header;
    boot_can_frame_t frame;
    uint8_t payload[3] = {1U, 2U, 3U};

    assert(boot_j1939_make_id(6U,
                              BOOT_J1939_PGN_PROPRIETARY_A,
                              TEST_DEVICE_ADDRESS,
                              TEST_TOOL_ADDRESS,
                              &identifier));
    assert(identifier == UINT32_C(0x18EF9080));
    assert(boot_j1939_decode_id(identifier, &header));
    assert(header.priority == 6U);
    assert(header.pgn == BOOT_J1939_PGN_PROPRIETARY_A);
    assert(header.source_address == TEST_DEVICE_ADDRESS);
    assert(header.destination_address == TEST_TOOL_ADDRESS);
    assert(header.destination_specific);

    assert(boot_j1939_make_id(6U,
                              BOOT_J1939_PGN_ADDRESS_CLAIMED,
                              TEST_DEVICE_ADDRESS,
                              BOOT_J1939_GLOBAL_ADDRESS,
                              &identifier));
    assert(identifier == UINT32_C(0x18EEFF80));

    assert(boot_j1939_make_id(6U,
                              BOOT_J1939_PGN_COMMANDED_ADDRESS,
                              TEST_DEVICE_ADDRESS,
                              0U,
                              &identifier));
    assert(identifier == UINT32_C(0x18FED880));
    assert(boot_j1939_decode_id(identifier, &header));
    assert(!header.destination_specific);
    assert(header.destination_address == BOOT_J1939_GLOBAL_ADDRESS);

    assert(!boot_j1939_make_id(8U, 0U, 0U, 0U, &identifier));
    assert(!boot_j1939_make_id(6U,
                               BOOT_J1939_PGN_PROPRIETARY_A | 1U,
                               0U,
                               0U,
                               &identifier));
    assert(!boot_j1939_decode_id(UINT32_C(0x02000000), &header));
    assert(!boot_j1939_decode_id(UINT32_C(0x18EAFFFF), &header));

    assert(boot_j1939_make_frame(&frame,
                                 6U,
                                 BOOT_J1939_PGN_REQUEST,
                                 TEST_TOOL_ADDRESS,
                                 BOOT_J1939_GLOBAL_ADDRESS,
                                 payload,
                                 sizeof(payload)));
    assert(frame.id_type == BOOT_CAN_ID_EXTENDED);
    assert(frame.dlc == sizeof(payload));
    assert(memcmp(frame.data, payload, sizeof(payload)) == 0);
    assert(frame.data[3] == UINT8_C(0xFF));
    frame.id_type = BOOT_CAN_ID_STANDARD;
    assert(!boot_j1939_decode_frame(&frame, &header));
}

static boot_can_frame_t make_control_request(void)
{
    boot_can_frame_t frame;
    uint8_t payload[8] = {
        BOOT_PROTOCOL_VERSION,
        BOOT_COMMAND_START_UPDATE,
        UINT8_C(0x5A),
        UINT8_C(0xC3),
        0U, 0U, 0U, 0U
    };

    boot_protocol_write_u32_le(&payload[4], UINT32_C(0x12345678));
    assert(boot_j1939_make_frame(&frame,
                                 BOOT_J1939_PRIORITY,
                                 BOOT_J1939_PGN_PROPRIETARY_A,
                                 TEST_TOOL_ADDRESS,
                                 TEST_DEVICE_ADDRESS,
                                 payload,
                                 sizeof(payload)));
    return frame;
}

static void test_proprietary_control_codec(void)
{
    boot_can_frame_t frame = make_control_request();
    boot_protocol_request_t request;
    boot_error_t error;

    assert(boot_protocol_decode_request(&frame, &request, &error));
    assert(error == BOOT_ERROR_NONE);
    assert(request.version == BOOT_PROTOCOL_VERSION);
    assert(request.command == BOOT_COMMAND_START_UPDATE);
    assert(request.session == UINT8_C(0x5A));
    assert(request.parameter == UINT8_C(0xC3));
    assert(request.value == UINT32_C(0x12345678));
    assert(request.source_address == TEST_TOOL_ADDRESS);
    assert(request.destination_address == TEST_DEVICE_ADDRESS);

    frame.data[0] = 1U;
    assert(!boot_protocol_decode_request(&frame, &request, &error));
    assert(error == BOOT_ERROR_UNSUPPORTED_VERSION);
    frame = make_control_request();
    frame.dlc = 7U;
    assert(!boot_protocol_decode_request(&frame, &request, &error));
    assert(error == BOOT_ERROR_MALFORMED_FRAME);
    frame = make_control_request();
    frame.id = (frame.id & ~UINT32_C(0xFF)) | BOOT_J1939_NULL_ADDRESS;
    assert(!boot_protocol_decode_request(&frame, &request, &error));
    assert(error == BOOT_ERROR_MALFORMED_FRAME);
    frame = make_control_request();
    frame.id = (frame.id & ~UINT32_C(0x0000FF00)) |
               ((uint32_t)BOOT_J1939_NULL_ADDRESS << 8U);
    assert(!boot_protocol_decode_request(&frame, &request, &error));
    assert(error == BOOT_ERROR_MALFORMED_FRAME);

    assert(boot_protocol_make_response(&frame,
                                       TEST_DEVICE_ADDRESS,
                                       TEST_TOOL_ADDRESS,
                                       BOOT_RESPONSE_NACK,
                                       BOOT_COMMAND_ABORT,
                                       UINT8_C(0x44),
                                       BOOT_ERROR_TIMEOUT));
    assert(frame.id == UINT32_C(0x18EF9080));
    assert(frame.data[0] == BOOT_PROTOCOL_VERSION);
    assert(frame.data[1] == BOOT_RESPONSE_NACK);
    assert(frame.data[2] == BOOT_COMMAND_ABORT);
    assert(frame.data[3] == UINT8_C(0x44));
    assert(boot_protocol_read_u32_le(&frame.data[4]) == BOOT_ERROR_TIMEOUT);
    assert(!boot_protocol_make_response(&frame,
                                        BOOT_J1939_NULL_ADDRESS,
                                        TEST_TOOL_ADDRESS,
                                        BOOT_RESPONSE_ACK,
                                        BOOT_COMMAND_IDENTIFY,
                                        1U,
                                        0U));
    assert(!boot_protocol_make_response(&frame,
                                        TEST_DEVICE_ADDRESS,
                                        BOOT_J1939_NULL_ADDRESS,
                                        BOOT_RESPONSE_ACK,
                                        BOOT_COMMAND_IDENTIFY,
                                        1U,
                                        0U));
}

static boot_can_frame_t make_dm14(uint16_t length,
                                  boot_dm14_command_t command,
                                  bool object,
                                  uint32_t pointer)
{
    boot_can_frame_t frame;
    uint8_t payload[8] = {0U};

    payload[0] = (uint8_t)length;
    payload[1] = (uint8_t)(((length >> 8U) & 7U) << 5U) |
                 (object ? UINT8_C(0x10) : 0U) |
                 (uint8_t)((uint8_t)command << 1U) |
                 UINT8_C(1);
    boot_protocol_write_u24_le(&payload[2], pointer);
    payload[5] = (uint8_t)(pointer >> 24U);
    boot_protocol_write_u16_le(&payload[6], UINT16_C(0xFFFF));
    assert(boot_j1939_make_frame(&frame,
                                 BOOT_J1939_PRIORITY,
                                 BOOT_J1939_PGN_DM14,
                                 TEST_TOOL_ADDRESS,
                                 TEST_DEVICE_ADDRESS,
                                 payload,
                                 sizeof(payload)));
    return frame;
}

static void test_dm14_dm15_codecs(void)
{
    boot_can_frame_t frame = make_dm14(100U,
                                       BOOT_DM14_COMMAND_WRITE,
                                       false,
                                       BOOT_APPLICATION_ADDRESS);
    boot_dm14_request_t request;
    boot_error_t error;

    assert(boot_protocol_decode_dm14(&frame, &request, &error));
    assert(request.length == 100U);
    assert(request.command == BOOT_DM14_COMMAND_WRITE);
    assert(!request.pointer_is_object);
    assert(request.pointer == BOOT_APPLICATION_ADDRESS);
    assert(request.key == UINT16_C(0xFFFF));
    assert(request.source_address == TEST_TOOL_ADDRESS);
    assert(request.destination_address == TEST_DEVICE_ADDRESS);

    frame = make_dm14(1784U,
                      BOOT_DM14_COMMAND_ERASE,
                      true,
                      UINT32_C(0x80ABCDEF));
    assert(boot_protocol_decode_dm14(&frame, &request, &error));
    assert(request.length == 1784U);
    assert(request.pointer_is_object);

    frame = make_dm14(1785U,
                      BOOT_DM14_COMMAND_WRITE,
                      false,
                      BOOT_APPLICATION_ADDRESS);
    assert(!boot_protocol_decode_dm14(&frame, &request, &error));
    assert(error == BOOT_ERROR_INVALID_SIZE);
    frame = make_dm14(1U,
                      BOOT_DM14_COMMAND_WRITE,
                      false,
                      BOOT_APPLICATION_ADDRESS);
    frame.data[1] &= (uint8_t)~UINT8_C(1);
    assert(!boot_protocol_decode_dm14(&frame, &request, &error));
    assert(error == BOOT_ERROR_MALFORMED_FRAME);

    assert(boot_protocol_make_dm15(&frame,
                                   TEST_DEVICE_ADDRESS,
                                   TEST_TOOL_ADDRESS,
                                   BOOT_DM15_STATUS_PROCEED,
                                   100U,
                                   UINT32_C(0x00ABCDEF),
                                   UINT8_C(6),
                                   UINT16_C(0xFFFF)));
    assert(frame.id == UINT32_C(0x18D89080));
    assert(frame.data[0] == 100U);
    assert(frame.data[1] == UINT8_C(0x11));
    assert(boot_protocol_read_u24_le(&frame.data[2]) == UINT32_C(0x00ABCDEF));
    assert(frame.data[5] == 6U);
    assert(boot_protocol_read_u16_le(&frame.data[6]) == UINT16_C(0xFFFF));
    assert(!boot_protocol_make_dm15(&frame,
                                    TEST_DEVICE_ADDRESS,
                                    TEST_TOOL_ADDRESS,
                                    BOOT_DM15_STATUS_PROCEED,
                                    1785U,
                                    0U,
                                    UINT8_C(0xFF),
                                    UINT16_C(0xFFFF)));
    assert(!boot_protocol_make_dm15(&frame,
                                    TEST_DEVICE_ADDRESS,
                                    BOOT_J1939_NULL_ADDRESS,
                                    BOOT_DM15_STATUS_PROCEED,
                                    1U,
                                    0U,
                                    0U,
                                    UINT16_C(0xFFFF)));
}

static void test_dm16_codec(void)
{
    uint8_t single[8] = {7U, 1U, 2U, 3U, 4U, 5U, 6U, 7U};
    uint8_t multipacket[101];
    boot_dm16_payload_t decoded;
    boot_error_t error;
    size_t index;

    assert(boot_protocol_decode_dm16(single, sizeof(single), &decoded, &error));
    assert(decoded.data_length == 7U);
    assert(decoded.data == &single[1]);

    multipacket[0] = UINT8_C(0xFF);
    for (index = 1U; index < sizeof(multipacket); ++index)
    {
        multipacket[index] = (uint8_t)index;
    }
    assert(boot_protocol_decode_dm16(multipacket,
                                     sizeof(multipacket),
                                     &decoded,
                                     &error));
    assert(decoded.data_length == 100U);
    assert(decoded.data[0] == 1U);

    single[0] = 0U;
    assert(!boot_protocol_decode_dm16(single, sizeof(single), &decoded, &error));
    assert(error == BOOT_ERROR_INVALID_SIZE);
    single[0] = 8U;
    assert(!boot_protocol_decode_dm16(single, sizeof(single), &decoded, &error));
    multipacket[0] = UINT8_C(0xFF);
    assert(!boot_protocol_decode_dm16(multipacket, 8U, &decoded, &error));
}

int main(void)
{
    test_crc32_known_vector();
    test_little_endian_helpers();
    test_j1939_identifier_mapping();
    test_proprietary_control_codec();
    test_dm14_dm15_codecs();
    test_dm16_codec();

    puts("test_protocol: OK");
    return 0;
}
