"""Small, backend-independent J1939 identifier and NAME primitives."""

from dataclasses import dataclass

from .constants import (
    DEFAULT_PRIORITY,
    GLOBAL_ADDRESS,
    MAX_UNICAST_ADDRESS,
    MIN_UNICAST_ADDRESS,
    NULL_ADDRESS,
)
from .errors import ConfigurationError, ProtocolError


MAX_J1939_ID = 0x1FFFFFFF
MAX_PGN = 0x1FFFF


def validate_unicast_address(address: int, name: str = "address") -> None:
    if not MIN_UNICAST_ADDRESS <= address <= MAX_UNICAST_ADDRESS:
        raise ConfigurationError(f"{name} must be between 0x00 and 0xFD")


def validate_source_address(address: int, *, allow_null: bool = False) -> None:
    maximum = NULL_ADDRESS if allow_null else MAX_UNICAST_ADDRESS
    if not MIN_UNICAST_ADDRESS <= address <= maximum:
        qualifier = "0xFE" if allow_null else "0xFD"
        raise ConfigurationError(
            f"source address must be between 0x00 and {qualifier}"
        )


@dataclass(frozen=True, slots=True)
class CanFrame:
    arbitration_id: int
    data: bytes
    is_extended_id: bool = True
    is_remote_frame: bool = False
    is_error_frame: bool = False

    def __post_init__(self) -> None:
        payload = bytes(self.data)
        maximum_id = MAX_J1939_ID if self.is_extended_id else 0x7FF
        if not 0 <= self.arbitration_id <= maximum_id:
            raise ProtocolError("CAN arbitration ID is out of range")
        if len(payload) > 8:
            raise ProtocolError("classic CAN payload cannot exceed 8 bytes")
        if self.is_remote_frame and payload:
            raise ProtocolError("remote frames cannot contain a payload")
        object.__setattr__(self, "data", payload)

    @property
    def dlc(self) -> int:
        return len(self.data)


@dataclass(frozen=True, slots=True)
class J1939Identifier:
    """Decoded J1939 29-bit identifier with a canonical PGN."""

    priority: int
    pgn: int
    source_address: int
    destination_address: int | None = None

    def __post_init__(self) -> None:
        if not 0 <= self.priority <= 7:
            raise ConfigurationError("priority must be between 0 and 7")
        if not 0 <= self.pgn <= MAX_PGN:
            raise ConfigurationError("PGN must be an unsigned 17-bit value")
        validate_source_address(self.source_address, allow_null=True)
        pf = (self.pgn >> 8) & 0xFF
        if pf < 0xF0:
            if self.pgn & 0xFF:
                raise ConfigurationError(
                    "PDU1 PGN must be canonical (low byte must be zero)"
                )
            if self.destination_address is None:
                raise ConfigurationError("PDU1 identifier requires a destination")
            if not 0 <= self.destination_address <= GLOBAL_ADDRESS:
                raise ConfigurationError("destination address must be 0x00..0xFF")
        elif self.destination_address is not None:
            raise ConfigurationError("PDU2 identifier cannot carry a destination")

    @property
    def is_pdu1(self) -> bool:
        return ((self.pgn >> 8) & 0xFF) < 0xF0

    def encode(self) -> int:
        value = (self.priority << 26) | (self.pgn << 8) | self.source_address
        if self.is_pdu1:
            assert self.destination_address is not None
            value |= self.destination_address << 8
        return value

    @classmethod
    def decode(cls, arbitration_id: int) -> "J1939Identifier":
        if not 0 <= arbitration_id <= MAX_J1939_ID:
            raise ProtocolError("J1939 arbitration ID is out of range")
        if arbitration_id & (1 << 25):
            raise ProtocolError("reserved J1939 identifier bit must be zero")
        priority = (arbitration_id >> 26) & 0x7
        source = arbitration_id & 0xFF
        if source > NULL_ADDRESS:
            raise ProtocolError("J1939 source address 0xFF is not valid")
        raw_pgn = (arbitration_id >> 8) & MAX_PGN
        pf = (arbitration_id >> 16) & 0xFF
        if pf < 0xF0:
            destination = (arbitration_id >> 8) & 0xFF
            pgn = raw_pgn & 0x1FF00
        else:
            destination = None
            pgn = raw_pgn
        return cls(priority, pgn, source, destination)


def make_frame(
    pgn: int,
    source_address: int,
    data: bytes | bytearray | memoryview,
    *,
    destination_address: int | None = None,
    priority: int = DEFAULT_PRIORITY,
) -> CanFrame:
    identifier = J1939Identifier(
        priority=priority,
        pgn=pgn,
        source_address=source_address,
        destination_address=destination_address,
    )
    return CanFrame(identifier.encode(), bytes(data), is_extended_id=True)


def identifier_from_frame(frame: CanFrame) -> J1939Identifier:
    if not frame.is_extended_id:
        raise ProtocolError("J1939 frame must use a 29-bit extended identifier")
    if frame.is_remote_frame or frame.is_error_frame:
        raise ProtocolError("J1939 data frame cannot be RTR or an error frame")
    return J1939Identifier.decode(frame.arbitration_id)


@dataclass(frozen=True, slots=True, order=True)
class J1939Name:
    """A 64-bit J1939 NAME, ordered by address-claim priority."""

    value: int

    def __post_init__(self) -> None:
        if not 0 <= self.value <= 0xFFFFFFFFFFFFFFFF:
            raise ConfigurationError("J1939 NAME must be an unsigned 64-bit value")

    def to_bytes(self) -> bytes:
        return self.value.to_bytes(8, "little")

    @classmethod
    def from_bytes(cls, payload: bytes | bytearray | memoryview) -> "J1939Name":
        data = bytes(payload)
        if len(data) != 8:
            raise ProtocolError("Address Claimed payload must contain exactly 8 bytes")
        return cls(int.from_bytes(data, "little"))

    @classmethod
    def from_fields(
        cls,
        *,
        identity_number: int,
        manufacturer_code: int,
        ecu_instance: int = 0,
        function_instance: int = 0,
        function: int = 0,
        vehicle_system: int = 0,
        vehicle_system_instance: int = 0,
        industry_group: int = 0,
        arbitrary_address_capable: bool = True,
    ) -> "J1939Name":
        fields = (
            (identity_number, 21, "identity number"),
            (manufacturer_code, 11, "manufacturer code"),
            (ecu_instance, 3, "ECU instance"),
            (function_instance, 5, "function instance"),
            (function, 8, "function"),
            (vehicle_system, 7, "vehicle system"),
            (vehicle_system_instance, 4, "vehicle system instance"),
            (industry_group, 3, "industry group"),
        )
        for value, width, label in fields:
            if not 0 <= value < (1 << width):
                raise ConfigurationError(f"{label} does not fit in {width} bits")
        value = identity_number
        value |= manufacturer_code << 21
        value |= ecu_instance << 32
        value |= function_instance << 35
        value |= function << 40
        value |= vehicle_system << 49
        value |= vehicle_system_instance << 56
        value |= industry_group << 60
        value |= int(arbitrary_address_capable) << 63
        return cls(value)

    @property
    def identity_number(self) -> int:
        return self.value & 0x1FFFFF

    @property
    def manufacturer_code(self) -> int:
        return (self.value >> 21) & 0x7FF

    @property
    def ecu_instance(self) -> int:
        return (self.value >> 32) & 0x7

    @property
    def function_instance(self) -> int:
        return (self.value >> 35) & 0x1F

    @property
    def function(self) -> int:
        return (self.value >> 40) & 0xFF

    @property
    def vehicle_system(self) -> int:
        return (self.value >> 49) & 0x7F

    @property
    def vehicle_system_instance(self) -> int:
        return (self.value >> 56) & 0xF

    @property
    def industry_group(self) -> int:
        return (self.value >> 60) & 0x7

    @property
    def arbitrary_address_capable(self) -> bool:
        return bool((self.value >> 63) & 1)

    def __str__(self) -> str:
        return f"0x{self.value:016X}"
