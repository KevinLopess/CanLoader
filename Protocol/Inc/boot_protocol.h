#ifndef BOOT_PROTOCOL_H
#define BOOT_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

#include "boot_can.h"

typedef enum
{
    BOOT_COMMAND_IDENTIFY = 0x01,
    BOOT_COMMAND_INFO = 0x02,
    BOOT_COMMAND_START_UPDATE = 0x10,
    BOOT_COMMAND_SET_IMAGE_CRC = 0x11,
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
    BOOT_ERROR_INTERNAL = 15,
    BOOT_ERROR_BUSY = 16,
    BOOT_ERROR_UNSUPPORTED_OPERATION = 17
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
    BOOT_INFO_UID_WORD_2 = 7,
    BOOT_INFO_NAME_LOW = 8,
    BOOT_INFO_NAME_HIGH = 9,
    BOOT_INFO_SOURCE_ADDRESS = 10
} boot_info_selector_t;

#define BOOT_CAPABILITY_APPLICATION_VALID   UINT32_C(1)
#define BOOT_CAPABILITY_UPDATE_ACTIVE       UINT32_C(2)
#define BOOT_CAPABILITY_APPLICATION_RUNNING UINT32_C(4)
#define BOOT_CAPABILITY_J1939                UINT32_C(8)
#define BOOT_CAPABILITY_ADDRESS_CLAIM        UINT32_C(16)
#define BOOT_CAPABILITY_TP_RX                UINT32_C(32)

typedef struct
{
    uint8_t version;
    boot_command_t command;
    uint8_t session;
    uint8_t parameter;
    uint32_t value;
    uint8_t source_address;
    uint8_t destination_address;
} boot_protocol_request_t;

typedef enum
{
    BOOT_DM14_COMMAND_ERASE = 0,
    BOOT_DM14_COMMAND_READ = 1,
    BOOT_DM14_COMMAND_WRITE = 2,
    BOOT_DM14_COMMAND_STATUS_REQUEST = 3,
    BOOT_DM14_COMMAND_OPERATION_COMPLETED = 4,
    BOOT_DM14_COMMAND_OPERATION_FAILED = 5,
    BOOT_DM14_COMMAND_BOOT_LOAD = 6,
    BOOT_DM14_COMMAND_EDCP_GENERATION = 7
} boot_dm14_command_t;

typedef enum
{
    BOOT_DM15_STATUS_PROCEED = 0,
    BOOT_DM15_STATUS_BUSY = 1,
    BOOT_DM15_STATUS_OPERATION_COMPLETED = 4,
    BOOT_DM15_STATUS_OPERATION_FAILED = 5
} boot_dm15_status_t;

typedef struct
{
    uint16_t length;
    uint32_t pointer;
    uint16_t key;
    boot_dm14_command_t command;
    bool pointer_is_object;
    uint8_t source_address;
    uint8_t destination_address;
} boot_dm14_request_t;

typedef struct
{
    uint16_t data_length;
    const uint8_t *data;
} boot_dm16_payload_t;

uint16_t boot_protocol_read_u16_le(const uint8_t *data);
uint32_t boot_protocol_read_u24_le(const uint8_t *data);
uint32_t boot_protocol_read_u32_le(const uint8_t *data);
uint64_t boot_protocol_read_u64_le(const uint8_t *data);
void boot_protocol_write_u16_le(uint8_t *data, uint16_t value);
void boot_protocol_write_u24_le(uint8_t *data, uint32_t value);
void boot_protocol_write_u32_le(uint8_t *data, uint32_t value);
void boot_protocol_write_u64_le(uint8_t *data, uint64_t value);

bool boot_protocol_decode_request(const boot_can_frame_t *frame,
                                  boot_protocol_request_t *request,
                                  boot_error_t *error);
bool boot_protocol_make_response(boot_can_frame_t *frame,
                                 uint8_t source_address,
                                 uint8_t destination_address,
                                 boot_response_kind_t kind,
                                 boot_command_t command,
                                 uint8_t session,
                                 uint32_t value);
bool boot_protocol_decode_dm14(const boot_can_frame_t *frame,
                               boot_dm14_request_t *request,
                               boot_error_t *error);
bool boot_protocol_make_dm15(boot_can_frame_t *frame,
                             uint8_t source_address,
                             uint8_t destination_address,
                             boot_dm15_status_t status,
                             uint16_t length,
                             uint32_t error_indicator,
                             uint8_t edcp_extension,
                             uint16_t seed);
bool boot_protocol_decode_dm16(const uint8_t *payload,
                               uint16_t payload_length,
                               boot_dm16_payload_t *data,
                               boot_error_t *error);

#endif /* BOOT_PROTOCOL_H */
