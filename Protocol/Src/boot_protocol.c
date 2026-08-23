#include "boot_protocol.h"

#include <stddef.h>

#include "boot_config.h"

uint16_t boot_protocol_read_u16_le(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8U));
}

uint32_t boot_protocol_read_u32_le(const uint8_t *data)
{
    return ((uint32_t)data[0]) |
           ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) |
           ((uint32_t)data[3] << 24U);
}

void boot_protocol_write_u16_le(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
}

void boot_protocol_write_u32_le(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

bool boot_protocol_decode_request(const boot_can_frame_t *frame,
                                  uint8_t node_id,
                                  boot_protocol_request_t *request,
                                  boot_error_t *error)
{
    if ((frame == NULL) || (request == NULL) || (error == NULL))
    {
        return false;
    }

    if (((frame->id != BOOT_CAN_COMMAND_ID(node_id)) &&
         (frame->id != BOOT_CAN_BROADCAST_ID)) ||
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

    if (request->version != BOOT_PROTOCOL_VERSION)
    {
        *error = BOOT_ERROR_UNSUPPORTED_VERSION;
        return false;
    }

    *error = BOOT_ERROR_NONE;
    return true;
}

bool boot_protocol_decode_block(const boot_protocol_request_t *request,
                                boot_protocol_block_t *block,
                                boot_error_t *error)
{
    if ((request == NULL) || (block == NULL) || (error == NULL))
    {
        return false;
    }

    if ((request->command != BOOT_COMMAND_BEGIN_BLOCK) ||
        ((request->value >> 24U) != 0U))
    {
        *error = BOOT_ERROR_MALFORMED_FRAME;
        return false;
    }

    block->block_sequence = request->parameter;
    block->block_index = (uint16_t)(request->value & UINT32_C(0xFFFF));
    block->block_length = (uint8_t)((request->value >> 16U) & UINT32_C(0xFF));

    if ((block->block_length == 0U) ||
        ((uint32_t)block->block_length > BOOT_TRANSFER_BLOCK_SIZE))
    {
        *error = BOOT_ERROR_INVALID_SIZE;
        return false;
    }

    *error = BOOT_ERROR_NONE;
    return true;
}

bool boot_protocol_decode_data(const boot_can_frame_t *frame,
                               uint8_t node_id,
                               boot_protocol_data_t *data,
                               boot_error_t *error)
{
    uint8_t index;

    if ((frame == NULL) || (data == NULL) || (error == NULL))
    {
        return false;
    }

    if ((frame->id != BOOT_CAN_DATA_ID(node_id)) || (frame->dlc < 4U) ||
        (frame->dlc > BOOT_CAN_MAX_DATA_LENGTH))
    {
        *error = BOOT_ERROR_MALFORMED_FRAME;
        return false;
    }

    data->session = frame->data[0];
    data->block_sequence = frame->data[1];
    data->frame_sequence = frame->data[2];
    data->payload_length = (uint8_t)(frame->dlc - 3U);

    for (index = 0U; index < data->payload_length; ++index)
    {
        data->payload[index] = frame->data[index + 3U];
    }
    for (; index < BOOT_DATA_BYTES_PER_FRAME; ++index)
    {
        data->payload[index] = UINT8_C(0xFF);
    }

    *error = BOOT_ERROR_NONE;
    return true;
}

void boot_protocol_make_response(boot_can_frame_t *frame,
                                 uint8_t node_id,
                                 boot_response_kind_t kind,
                                 boot_command_t command,
                                 uint8_t session,
                                 uint32_t value)
{
    if (frame == NULL)
    {
        return;
    }

    frame->id = BOOT_CAN_RESPONSE_ID(node_id);
    frame->dlc = BOOT_CAN_MAX_DATA_LENGTH;
    frame->data[0] = BOOT_PROTOCOL_VERSION;
    frame->data[1] = (uint8_t)kind;
    frame->data[2] = (uint8_t)command;
    frame->data[3] = session;
    boot_protocol_write_u32_le(&frame->data[4], value);
}
