"""Wire-level constants for the CanLoader J1939 protocol profile."""

from enum import IntEnum


PROTOCOL_VERSION = 2
TARGET_CODE_STM32F103C8 = 0xF1
TARGET_CODE_STM32G0B1RE = 0xB1
SUPPORTED_TARGET_CODES = frozenset(
    (TARGET_CODE_STM32F103C8, TARGET_CODE_STM32G0B1RE)
)
# Kept as an import-compatible alias for users of canloader 0.2.x.
TARGET_CODE = TARGET_CODE_STM32F103C8

# J1939 addresses.
NULL_ADDRESS = 0xFE
GLOBAL_ADDRESS = 0xFF
MIN_UNICAST_ADDRESS = 0x00
MAX_UNICAST_ADDRESS = 0xFD
DEFAULT_TARGET_ADDRESS = 0x80
DEFAULT_SOURCE_ADDRESS = 0xF9

# Lab/service-tool NAME. Deployments should pass an assigned unique NAME.
DEFAULT_SOURCE_NAME = 0xA000000000000001

# Parameter group numbers used by this profile.
PGN_DM16 = 0x00D700
PGN_DM15 = 0x00D800
PGN_DM14 = 0x00D900
PGN_REQUEST = 0x00EA00
PGN_TP_DT = 0x00EB00
PGN_TP_CM = 0x00EC00
PGN_ADDRESS_CLAIMED = 0x00EE00
PGN_PROPRIETARY_A = 0x00EF00
PGN_COMMANDED_ADDRESS = 0x00FED8

DEFAULT_PRIORITY = 6
ADDRESS_CLAIM_PRIORITY = 6
ADDRESS_CLAIM_WAIT = 0.250
AAC_FALLBACK_ADDRESSES = tuple(range(128, 248))

MAX_TP_PACKETS = 0xFF
TP_DATA_BYTES_PER_PACKET = 7
MAX_TP_PAYLOAD = MAX_TP_PACKETS * TP_DATA_BYTES_PER_PACKET
MAX_DM16_DATA = MAX_TP_PAYLOAD - 1
UPDATE_BLOCK_SIZE = 100
# CanLoader targets now advertise their erase unit through INFO. This is only
# the compatibility fallback for protocol-v2 firmware built before selector 11.
FLASH_ERASE_BLOCK_SIZE = 1024
MIN_IMAGE_SIZE = 8


class TpControl(IntEnum):
    RTS = 0x10
    CTS = 0x11
    EOM_ACK = 0x13
    BAM = 0x20
    ABORT = 0xFF


class TpAbortReason(IntEnum):
    BUSY = 1
    RESOURCES = 2
    TIMEOUT = 3
    CTS_WHILE_TRANSFER = 4
    MAX_RETRANSMIT = 5
    UNEXPECTED_DATA = 6
    BAD_SEQUENCE = 7
    DUPLICATE_SEQUENCE = 8
    TOTAL_SIZE = 9
    OTHER = 250


class Command(IntEnum):
    IDENTIFY = 0x01
    INFO = 0x02
    START_UPDATE = 0x10
    SET_IMAGE_CRC = 0x11
    FINALIZE = 0x30
    VERIFY = 0x31
    RUN_APPLICATION = 0x32
    ENTER_BOOTLOADER = 0x33
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
    UID_WORD_0 = 5
    UID_WORD_1 = 6
    UID_WORD_2 = 7
    NAME_LOW = 8
    NAME_HIGH = 9
    SOURCE_ADDRESS = 10
    FLASH_ERASE_UNIT_SIZE = 11
    # Optional test-application INFO extension; not implemented by bootloader.
    ACTIVITY_VERSION = 0x80
    ACTIVITY_TX_QUEUED = 0x81
    ACTIVITY_RX_VALID = 0x82
    ACTIVITY_TX_SKIPPED = 0x83
    ACTIVITY_LAST_RX_ADDRESS = 0x84
    ACTIVITY_LAST_RX_SEQUENCE = 0x85
    ACTIVITY_LAST_RX_AGE_MS = 0x86
    ACTIVITY_RX_OVERRUNS = 0x87
    ACTIVITY_CAN_STATUS = 0x88
    ACTIVITY_PERIOD_MS = 0x89


class Dm14Command(IntEnum):
    ERASE = 0
    READ = 1
    WRITE = 2
    STATUS = 3
    OPERATION_COMPLETED = 4
    OPERATION_FAILED = 5
    BOOT_LOAD = 6
    EDCP = 7


class Dm15Status(IntEnum):
    PROCEED = 0
    BUSY = 1
    OPERATION_COMPLETED = 4
    OPERATION_FAILED = 5


class PointerType(IntEnum):
    DIRECT = 0
    INDIRECT = 1


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
    BUSY = 16
    UNSUPPORTED_OPERATION = 17


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
    NackCode.MALFORMED_FRAME: "malformed frame",
    NackCode.SESSION: "session does not match",
    NackCode.CAN: "device CAN error",
    NackCode.INTERNAL: "device internal error",
    NackCode.BUSY: "device is busy",
    NackCode.UNSUPPORTED_OPERATION: "operation is not supported",
}


CAPABILITY_APPLICATION_VALID = 1 << 0
CAPABILITY_UPDATE_ACTIVE = 1 << 1
CAPABILITY_APPLICATION_RUNNING = 1 << 2
CAPABILITY_J1939 = 1 << 3
CAPABILITY_ADDRESS_CLAIM = 1 << 4
CAPABILITY_TP_RX = 1 << 5
REQUIRED_PROFILE_CAPABILITIES = (
    CAPABILITY_J1939 | CAPABILITY_ADDRESS_CLAIM | CAPABILITY_TP_RX
)
