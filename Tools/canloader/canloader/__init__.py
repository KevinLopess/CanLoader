"""Host implementation of the CanLoader CAN bootloader protocol."""

from .address_claim import AddressBook, AddressClaimManager
from .client import (
    BootloaderClient,
    DeviceInfo,
    DiscoveredDevice,
    NetworkScanner,
    UpdateResult,
)
from .constants import (
    Command,
    Dm14Command,
    Dm15Status,
    InfoSelector,
    NackCode,
    ResponseKind,
)
from .crc import crc32_iso_hdlc
from .errors import (
    AddressClaimError,
    AddressConflictError,
    CanLoaderError,
    DeviceSessionTimeout,
    NackError,
    ProtocolError,
    ResponseTimeout,
    TargetIdentityError,
    TransportError,
)
from .j1939 import CanFrame, J1939Identifier, J1939Name
from .transport import J1939Transport

__all__ = [
    "BootloaderClient",
    "AddressClaimError",
    "AddressClaimManager",
    "AddressBook",
    "AddressConflictError",
    "CanFrame",
    "CanLoaderError",
    "Command",
    "DeviceInfo",
    "DiscoveredDevice",
    "DeviceSessionTimeout",
    "Dm14Command",
    "Dm15Status",
    "InfoSelector",
    "NackCode",
    "NackError",
    "NetworkScanner",
    "J1939Identifier",
    "J1939Name",
    "J1939Transport",
    "ProtocolError",
    "ResponseKind",
    "ResponseTimeout",
    "TargetIdentityError",
    "TransportError",
    "UpdateResult",
    "crc32_iso_hdlc",
]

__version__ = "0.2.0"
