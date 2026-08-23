"""Host implementation of the CanLoader CAN bootloader protocol."""

from .client import (
    BootloaderClient,
    DeviceInfo,
    DiscoveredDevice,
    NetworkScanner,
    UpdateResult,
)
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
    "DiscoveredDevice",
    "DeviceSessionTimeout",
    "InfoSelector",
    "NackCode",
    "NackError",
    "NetworkScanner",
    "ProtocolError",
    "ResponseKind",
    "ResponseTimeout",
    "UpdateResult",
    "crc32_iso_hdlc",
]

__version__ = "0.1.0"
