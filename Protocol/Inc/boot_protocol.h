#ifndef BOOT_PROTOCOL_H
#define BOOT_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

#include "boot_can.h"

typedef enum
{
    BOOT_COMMAND_DISCOVER = 0x01,
    BOOT_COMMAND_INFO = 0x02,
    BOOT_COMMAND_PROBE = 0x03,
    BOOT_COMMAND_SET_NODE_ID = 0x04,
    BOOT_COMMAND_START_UPDATE = 0x10,
    BOOT_COMMAND_SET_IMAGE_CRC = 0x11,
    BOOT_COMMAND_ERASE = 0x12,
    BOOT_COMMAND_BEGIN_BLOCK = 0x20,
    BOOT_COMMAND_DATA = 0x21,
    BOOT_COMMAND_FINISH = 0x30,
    BOOT_COMMAND_VERIFY = 0x31,
    BOOT_COMMAND_RUN_APPLICATION = 0x32,
    BOOT_COMMAND_ENTER_BOOTLOADER = 0x33,
    BOOT_COMMAND_ABORT = 0x3F
} boot_command_t;

typedef enum
{
    BOOT_RESPONSE_ACK = 0x80,
    BOOT_RESPONSE_NACK = 0x81
} boot_response_kind_t;

typedef enum
{
    BOOT_ERROR_NONE = 0,
    BOOT_ERROR_INVALID_COMMAND = 1,
    BOOT_ERROR_INVALID_STATE = 2,
    BOOT_ERROR_INVALID_SEQUENCE = 3,
    BOOT_ERROR_INVALID_ADDRESS = 4,
    BOOT_ERROR_INVALID_SIZE = 5,
    BOOT_ERROR_FLASH_ERASE = 6,
    BOOT_ERROR_FLASH_WRITE = 7,
    BOOT_ERROR_CRC_MISMATCH = 8,
    BOOT_ERROR_TIMEOUT = 9,
    BOOT_ERROR_UNSUPPORTED_VERSION = 10,
    BOOT_ERROR_INVALID_APPLICATION = 11,
    BOOT_ERROR_MALFORMED_FRAME = 12,
    BOOT_ERROR_SESSION = 13,
    BOOT_ERROR_CAN = 14,
    BOOT_ERROR_INTERNAL = 15
} boot_error_t;

typedef enum
{
    BOOT_INFO_CAPABILITIES = 0,
    BOOT_INFO_APPLICATION_BASE = 1,
    BOOT_INFO_APPLICATION_MAX_SIZE = 2,
    BOOT_INFO_STORED_IMAGE_SIZE = 3,
    BOOT_INFO_STORED_IMAGE_CRC = 4,
    BOOT_INFO_UID_WORD_0 = 5,
    BOOT_INFO_UID_WORD_1 = 6,
    BOOT_INFO_UID_WORD_2 = 7
} boot_info_selector_t;

typedef enum
{
    BOOT_PROBE_CAPABILITIES = 0,
    BOOT_PROBE_UID_WORD_0 = 1,
    BOOT_PROBE_UID_WORD_1 = 2,
    BOOT_PROBE_UID_WORD_2 = 3
} boot_probe_selector_t;

#define BOOT_CAPABILITY_APPLICATION_VALID   UINT32_C(1)
#define BOOT_CAPABILITY_UPDATE_ACTIVE       UINT32_C(2)
#define BOOT_CAPABILITY_APPLICATION_RUNNING UINT32_C(4)

typedef struct
{
    uint8_t version;
    boot_command_t command;
    uint8_t session;
    uint8_t parameter;
    uint32_t value;
} boot_protocol_request_t;

typedef struct
{
    uint8_t session;
    uint8_t block_sequence;
    uint8_t frame_sequence;
    uint8_t payload_length;
    uint8_t payload[5];
} boot_protocol_data_t;

typedef struct
{
    uint8_t block_sequence;
    uint16_t block_index;
    uint8_t block_length;
} boot_protocol_block_t;

uint16_t boot_protocol_read_u16_le(const uint8_t *data);
uint32_t boot_protocol_read_u32_le(const uint8_t *data);
void boot_protocol_write_u16_le(uint8_t *data, uint16_t value);
void boot_protocol_write_u32_le(uint8_t *data, uint32_t value);

bool boot_protocol_decode_request(const boot_can_frame_t *frame,
                                  uint8_t node_id,
                                  boot_protocol_request_t *request,
                                  boot_error_t *error);
bool boot_protocol_decode_block(const boot_protocol_request_t *request,
                                boot_protocol_block_t *block,
                                boot_error_t *error);
bool boot_protocol_decode_data(const boot_can_frame_t *frame,
                               uint8_t node_id,
                               boot_protocol_data_t *data,
                               boot_error_t *error);
void boot_protocol_make_response(boot_can_frame_t *frame,
                                 uint8_t node_id,
                                 boot_response_kind_t kind,
                                 boot_command_t command,
                                 uint8_t session,
                                 uint32_t value);

#endif /* BOOT_PROTOCOL_H */
