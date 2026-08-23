#include "boot_protocol.h"

#include <stddef.h>

#include "boot_config.h"
#include "boot_j1939.h"

#define DM_MAX_LENGTH             UINT16_C(1784)
#define DM16_MAX_PAYLOAD_LENGTH   UINT16_C(1785)
#define DM14_RESERVED_BIT         UINT8_C(0x01)
#define DM14_POINTER_TYPE_BIT     UINT8_C(0x10)
#define DM15_RESERVED_BITS        UINT8_C(0x11)
#define DM_ERROR_INDICATOR_MAX    UINT32_C(0x00FFFFFF)

uint16_t boot_protocol_read_u16_le(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8U));
}

uint32_t boot_protocol_read_u24_le(const uint8_t *data)
{
    return ((uint32_t)data[0]) |
           ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U);
}

uint32_t boot_protocol_read_u32_le(const uint8_t *data)
{
    return boot_protocol_read_u24_le(data) | ((uint32_t)data[3] << 24U);
}

uint64_t boot_protocol_read_u64_le(const uint8_t *data)
{
    return ((uint64_t)boot_protocol_read_u32_le(data)) |
           ((uint64_t)boot_protocol_read_u32_le(&data[4]) << 32U);
}

void boot_protocol_write_u16_le(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
}

void boot_protocol_write_u24_le(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
}

void boot_protocol_write_u32_le(uint8_t *data, uint32_t value)
{
    boot_protocol_write_u24_le(data, value);
    data[3] = (uint8_t)(value >> 24U);
}

void boot_protocol_write_u64_le(uint8_t *data, uint64_t value)
{
    boot_protocol_write_u32_le(data, (uint32_t)value);
    boot_protocol_write_u32_le(&data[4], (uint32_t)(value >> 32U));
}

bool boot_protocol_decode_request(const boot_can_frame_t *frame,
                                  boot_protocol_request_t *request,
                                  boot_error_t *error)
{
    boot_j1939_header_t header;

    if ((frame == NULL) || (request == NULL) || (error == NULL))
    {
        return false;
    }
    if (!boot_j1939_decode_frame(frame, &header) ||
        (header.pgn != BOOT_J1939_PGN_PROPRIETARY_A) ||
        !header.destination_specific ||
        (header.source_address >= BOOT_J1939_NULL_ADDRESS) ||
        (header.destination_address >= BOOT_J1939_NULL_ADDRESS) ||
        (frame->dlc != BOOT_CAN_MAX_DATA_LENGTH))
    {
        *error = BOOT_ERROR_MALFORMED_FRAME;
        return false;
    }

    request->version = frame->data[0];
    request->command = (boot_command_t)frame->data[1];
    request->session = frame->data[2];
    request->parameter = frame->data[3];
    request->value = boot_protocol_read_u32_le(&frame->data[4]);
    request->source_address = header.source_address;
    request->destination_address = header.destination_address;

    if (request->version != BOOT_PROTOCOL_VERSION)
    {
        *error = BOOT_ERROR_UNSUPPORTED_VERSION;
        return false;
    }

    *error = BOOT_ERROR_NONE;
    return true;
}

bool boot_protocol_make_response(boot_can_frame_t *frame,
                                 uint8_t source_address,
                                 uint8_t destination_address,
                                 boot_response_kind_t kind,
                                 boot_command_t command,
                                 uint8_t session,
                                 uint32_t value)
{
    uint8_t data[BOOT_CAN_MAX_DATA_LENGTH];

    if ((source_address >= BOOT_J1939_NULL_ADDRESS) ||
        (destination_address >= BOOT_J1939_NULL_ADDRESS))
    {
        return false;
    }

    data[0] = BOOT_PROTOCOL_VERSION;
    data[1] = (uint8_t)kind;
    data[2] = (uint8_t)command;
    data[3] = session;
    boot_protocol_write_u32_le(&data[4], value);

    return boot_j1939_make_frame(frame,
                                 BOOT_J1939_PRIORITY,
                                 BOOT_J1939_PGN_PROPRIETARY_A,
                                 source_address,
                                 destination_address,
                                 data,
                                 BOOT_CAN_MAX_DATA_LENGTH);
}

bool boot_protocol_decode_dm14(const boot_can_frame_t *frame,
                               boot_dm14_request_t *request,
                               boot_error_t *error)
{
    boot_j1939_header_t header;
    uint16_t length;

    if ((frame == NULL) || (request == NULL) || (error == NULL))
    {
        return false;
    }
    if (!boot_j1939_decode_frame(frame, &header) ||
        (header.pgn != BOOT_J1939_PGN_DM14) ||
        !header.destination_specific ||
        (header.source_address >= BOOT_J1939_NULL_ADDRESS) ||
        (header.destination_address >= BOOT_J1939_NULL_ADDRESS) ||
        (frame->dlc != BOOT_CAN_MAX_DATA_LENGTH) ||
        ((frame->data[1] & DM14_RESERVED_BIT) == 0U))
    {
        *error = BOOT_ERROR_MALFORMED_FRAME;
        return false;
    }

    length = (uint16_t)((uint16_t)frame->data[0] |
                        ((uint16_t)(frame->data[1] >> 5U) << 8U));
    if (length > DM_MAX_LENGTH)
    {
        *error = BOOT_ERROR_INVALID_SIZE;
        return false;
    }

    request->length = length;
    request->pointer = boot_protocol_read_u24_le(&frame->data[2]) |
                       ((uint32_t)frame->data[5] << 24U);
    request->key = boot_protocol_read_u16_le(&frame->data[6]);
    request->command = (boot_dm14_command_t)((frame->data[1] >> 1U) & 7U);
    request->pointer_is_object =
        (frame->data[1] & DM14_POINTER_TYPE_BIT) != 0U;
    request->source_address = header.source_address;
    request->destination_address = header.destination_address;
    *error = BOOT_ERROR_NONE;
    return true;
}

bool boot_protocol_make_dm15(boot_can_frame_t *frame,
                             uint8_t source_address,
                             uint8_t destination_address,
                             boot_dm15_status_t status,
                             uint16_t length,
                             uint32_t error_indicator,
                             uint8_t edcp_extension,
                             uint16_t seed)
{
    uint8_t data[BOOT_CAN_MAX_DATA_LENGTH];

    if ((frame == NULL) ||
        (source_address >= BOOT_J1939_NULL_ADDRESS) ||
        (destination_address >= BOOT_J1939_NULL_ADDRESS) ||
        (length > DM_MAX_LENGTH) ||
        (error_indicator > DM_ERROR_INDICATOR_MAX) ||
        ((status != BOOT_DM15_STATUS_PROCEED) &&
         (status != BOOT_DM15_STATUS_BUSY) &&
         (status != BOOT_DM15_STATUS_OPERATION_COMPLETED) &&
         (status != BOOT_DM15_STATUS_OPERATION_FAILED)))
    {
        return false;
    }

    data[0] = (uint8_t)length;
    data[1] = (uint8_t)(((length >> 8U) & 7U) << 5U) |
              (uint8_t)((uint8_t)status << 1U) |
              DM15_RESERVED_BITS;
    boot_protocol_write_u24_le(&data[2], error_indicator);
    data[5] = edcp_extension;
    boot_protocol_write_u16_le(&data[6], seed);

    return boot_j1939_make_frame(frame,
                                 BOOT_J1939_PRIORITY,
                                 BOOT_J1939_PGN_DM15,
                                 source_address,
                                 destination_address,
                                 data,
                                 BOOT_CAN_MAX_DATA_LENGTH);
}

bool boot_protocol_decode_dm16(const uint8_t *payload,
                               uint16_t payload_length,
                               boot_dm16_payload_t *data,
                               boot_error_t *error)
{
    uint16_t raw_length;

    if ((payload == NULL) || (data == NULL) || (error == NULL))
    {
        return false;
    }
    if ((payload_length < 2U) ||
        (payload_length > DM16_MAX_PAYLOAD_LENGTH))
    {
        *error = BOOT_ERROR_INVALID_SIZE;
        return false;
    }

    if (payload[0] == UINT8_C(0xFF))
    {
        raw_length = (uint16_t)(payload_length - 1U);
        if ((raw_length <= 7U) || (raw_length > DM_MAX_LENGTH))
        {
            *error = BOOT_ERROR_INVALID_SIZE;
            return false;
        }
    }
    else
    {
        raw_length = payload[0];
        if ((raw_length == 0U) || (raw_length > 7U) ||
            (payload_length < (uint16_t)(raw_length + 1U)))
        {
            *error = BOOT_ERROR_INVALID_SIZE;
            return false;
        }
    }

    data->data_length = raw_length;
    data->data = &payload[1];
    *error = BOOT_ERROR_NONE;
    return true;
}
