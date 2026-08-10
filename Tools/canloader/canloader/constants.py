"""Wire-level constants shared by the CanLoader host modules."""

from enum import IntEnum


PROTOCOL_VERSION = 1

RESPONSE_ID_BASE = 0x580
COMMAND_ID_BASE = 0x600
DATA_ID_BASE = 0x700

MIN_NODE_ID = 1
MAX_NODE_ID = 0x7F
MAX_BLOCK_SIZE = 100
MAX_DATA_PAYLOAD = 5
MIN_IMAGE_SIZE = 8


class Command(IntEnum):
    DISCOVER = 0x01
    INFO = 0x02
    START_UPDATE = 0x10
    SET_IMAGE_CRC = 0x11
    ERASE = 0x12
    BEGIN_BLOCK = 0x20
    DATA = 0x21
    FINISH = 0x30
    VERIFY = 0x31
    RUN_APPLICATION = 0x32
    ABORT = 0x3F


class ResponseKind(IntEnum):
    ACK = 0x80
    NACK = 0x81


class InfoSelector(IntEnum):
    CAPABILITIES = 0
    APP_BASE = 1
    APP_MAX_SIZE = 2
    STORED_SIZE = 3
    STORED_CRC = 4


class NackCode(IntEnum):
    NONE = 0
    INVALID_COMMAND = 1
    INVALID_STATE = 2
    INVALID_SEQUENCE = 3
    INVALID_ADDRESS = 4
    INVALID_SIZE = 5
    FLASH_ERASE = 6
    FLASH_WRITE = 7
    CRC_MISMATCH = 8
    TIMEOUT = 9
    UNSUPPORTED_VERSION = 10
    INVALID_APPLICATION = 11
    MALFORMED_FRAME = 12
    SESSION = 13
    CAN = 14
    INTERNAL = 15


NACK_DESCRIPTIONS: dict[NackCode, str] = {
    NackCode.NONE: "no error",
    NackCode.INVALID_COMMAND: "invalid command",
    NackCode.INVALID_STATE: "command is not valid in the current state",
    NackCode.INVALID_SEQUENCE: "invalid block or frame sequence",
    NackCode.INVALID_ADDRESS: "address is outside the application region",
    NackCode.INVALID_SIZE: "invalid firmware or block size",
    NackCode.FLASH_ERASE: "flash erase failed",
    NackCode.FLASH_WRITE: "flash write failed",
    NackCode.CRC_MISMATCH: "firmware CRC does not match",
    NackCode.TIMEOUT: "device operation timed out",
    NackCode.UNSUPPORTED_VERSION: "unsupported protocol version",
    NackCode.INVALID_APPLICATION: "application image is invalid",
    NackCode.MALFORMED_FRAME: "malformed CAN frame",
    NackCode.SESSION: "session does not match",
    NackCode.CAN: "device CAN error",
    NackCode.INTERNAL: "device internal error",
}
