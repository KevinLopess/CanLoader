"""Host implementation of the CanLoader CAN bootloader protocol."""

from .client import BootloaderClient, DeviceInfo, UpdateResult
from .constants import Command, InfoSelector, NackCode, ResponseKind
from .crc import crc32_iso_hdlc
from .errors import (
    CanLoaderError,
    DeviceSessionTimeout,
    NackError,
    ProtocolError,
    ResponseTimeout,
)

__all__ = [
    "BootloaderClient",
    "CanLoaderError",
    "Command",
    "DeviceInfo",
    "DeviceSessionTimeout",
    "InfoSelector",
    "NackCode",
    "NackError",
    "ProtocolError",
    "ResponseKind",
    "ResponseTimeout",
    "UpdateResult",
    "crc32_iso_hdlc",
]

__version__ = "0.1.0"
