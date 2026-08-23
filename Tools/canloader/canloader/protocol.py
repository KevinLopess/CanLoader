"""CanLoader v2 control messages and the DM14/DM15/DM16 profile."""

from dataclasses import dataclass

from .constants import (
    GLOBAL_ADDRESS,
    MAX_DM16_DATA,
    PGN_DM14,
    PGN_DM15,
    PGN_DM16,
    PGN_PROPRIETARY_A,
    PROTOCOL_VERSION,
    Command,
    Dm14Command,
    Dm15Status,
    PointerType,
    ResponseKind,
)
from .errors import ConfigurationError, ProtocolError
from .j1939 import (
    CanFrame,
    J1939Identifier,
    J1939Name,
    identifier_from_frame,
    make_frame,
    validate_unicast_address,
)


def _require_byte(value: int, name: str, *, nonzero: bool = False) -> None:
    minimum = 1 if nonzero else 0
    if not minimum <= value <= 0xFF:
        qualifier = "nonzero " if nonzero else ""
        raise ConfigurationError(f"{name} must be a {qualifier}8-bit value")


def _require_u32(value: int, name: str) -> None:
    if not 0 <= value <= 0xFFFFFFFF:
        raise ConfigurationError(f"{name} must be an unsigned 32-bit value")


def _require_u24(value: int, name: str) -> None:
    if not 0 <= value <= 0xFFFFFF:
        raise ConfigurationError(f"{name} must be an unsigned 24-bit value")


def _directed_identifier(frame: CanFrame, pgn: int) -> J1939Identifier:
    identifier = identifier_from_frame(frame)
    if identifier.pgn != pgn:
        raise ProtocolError(f"expected PGN 0x{pgn:05X}")
    if identifier.destination_address in (None, GLOBAL_ADDRESS):
        raise ProtocolError("message must be destination-specific PDU1")
    return identifier


def _make_directed_frame(
    pgn: int, source_address: int, destination_address: int, payload: bytes
) -> CanFrame:
    validate_unicast_address(source_address, "source address")
    validate_unicast_address(destination_address, "destination address")
    return make_frame(
        pgn,
        source_address,
        payload,
        destination_address=destination_address,
    )


@dataclass(frozen=True, slots=True)
class ControlRequest:
    source_address: int
    destination_address: int
    version: int
    command: int
    session: int
    parameter: int
    value: int


@dataclass(frozen=True, slots=True)
class ControlResponse:
    source_address: int
    destination_address: int
    version: int
    kind: ResponseKind
    command: int
    session: int
    value: int


def encode_control_request(
    source_address: int,
    destination_address: int,
    command: Command | int,
    session: int,
    *,
    parameter: int = 0,
    value: int = 0,
) -> CanFrame:
    _require_byte(int(command), "command")
    _require_byte(session, "session", nonzero=True)
    _require_byte(parameter, "parameter")
    _require_u32(value, "control value")
    payload = bytes((PROTOCOL_VERSION, int(command), session, parameter))
    payload += value.to_bytes(4, "little")
    return _make_directed_frame(
        PGN_PROPRIETARY_A, source_address, destination_address, payload
    )


def decode_control_request(frame: CanFrame) -> ControlRequest:
    identifier = _directed_identifier(frame, PGN_PROPRIETARY_A)
    if frame.dlc != 8:
        raise ProtocolError("control request must contain exactly 8 bytes")
    return ControlRequest(
        source_address=identifier.source_address,
        destination_address=identifier.destination_address,
        version=frame.data[0],
        command=frame.data[1],
        session=frame.data[2],
        parameter=frame.data[3],
        value=int.from_bytes(frame.data[4:8], "little"),
    )


def encode_response(
    source_address: int,
    destination_address: int,
    kind: ResponseKind | int,
    command: Command | int,
    session: int,
    value: int = 0,
    *,
    version: int = PROTOCOL_VERSION,
) -> CanFrame:
    _require_byte(version, "version")
    _require_byte(int(kind), "response kind")
    _require_byte(int(command), "command")
    _require_byte(session, "session", nonzero=True)
    _require_u32(value, "response value")
    payload = bytes((version, int(kind), int(command), session))
    payload += value.to_bytes(4, "little")
    return _make_directed_frame(
        PGN_PROPRIETARY_A, source_address, destination_address, payload
    )


def decode_response(frame: CanFrame) -> ControlResponse:
    identifier = _directed_identifier(frame, PGN_PROPRIETARY_A)
    if frame.dlc != 8:
        raise ProtocolError("control response must contain exactly 8 bytes")
    try:
        kind = ResponseKind(frame.data[1])
    except ValueError as exc:
        raise ProtocolError(f"unknown response kind 0x{frame.data[1]:02X}") from exc
    return ControlResponse(
        source_address=identifier.source_address,
        destination_address=identifier.destination_address,
        version=frame.data[0],
        kind=kind,
        command=frame.data[2],
        session=frame.data[3],
        value=int.from_bytes(frame.data[4:8], "little"),
    )


@dataclass(frozen=True, slots=True)
class Dm14:
    source_address: int
    destination_address: int
    command: Dm14Command
    length: int
    pointer_type: PointerType
    pointer: int
    key: int


def encode_dm14(
    source_address: int,
    destination_address: int,
    command: Dm14Command | int,
    *,
    length: int,
    pointer: int,
    key: int = 0xFFFF,
    pointer_type: PointerType | int = PointerType.DIRECT,
) -> CanFrame:
    try:
        command_value = Dm14Command(command)
        pointer_type_value = PointerType(pointer_type)
    except ValueError as exc:
        raise ConfigurationError("invalid DM14 command or pointer type") from exc
    if not 0 <= length <= MAX_DM16_DATA:
        raise ConfigurationError(f"DM14 length must be 0..{MAX_DM16_DATA}")
    _require_u32(pointer, "DM14 pointer")
    if not 0 <= key <= 0xFFFF:
        raise ConfigurationError("DM14 key must be an unsigned 16-bit value")
    if command_value in (Dm14Command.ERASE, Dm14Command.READ, Dm14Command.WRITE):
        if length == 0:
            raise ConfigurationError("DM14 data operation length cannot be zero")
    second = ((length >> 8) & 0x07) << 5
    second |= int(pointer_type_value) << 4
    second |= int(command_value) << 1
    second |= 0x01
    payload = bytes((length & 0xFF, second))
    payload += pointer.to_bytes(4, "little")
    payload += key.to_bytes(2, "little")
    return _make_directed_frame(PGN_DM14, source_address, destination_address, payload)


def decode_dm14(frame: CanFrame) -> Dm14:
    identifier = _directed_identifier(frame, PGN_DM14)
    if frame.dlc != 8:
        raise ProtocolError("DM14 must contain exactly 8 bytes")
    if (frame.data[1] & 0x01) == 0:
        raise ProtocolError("DM14 reserved bit must be one")
    length = frame.data[0] | ((frame.data[1] >> 5) << 8)
    if length > MAX_DM16_DATA:
        raise ProtocolError(f"DM14 length exceeds profile limit {MAX_DM16_DATA}")
    try:
        command = Dm14Command((frame.data[1] >> 1) & 0x07)
        pointer_type = PointerType((frame.data[1] >> 4) & 1)
    except ValueError as exc:
        raise ProtocolError("invalid DM14 command or pointer type") from exc
    return Dm14(
        source_address=identifier.source_address,
        destination_address=identifier.destination_address,
        command=command,
        length=length,
        pointer_type=pointer_type,
        pointer=int.from_bytes(frame.data[2:6], "little"),
        key=int.from_bytes(frame.data[6:8], "little"),
    )


@dataclass(frozen=True, slots=True)
class Dm15:
    source_address: int
    destination_address: int
    status: Dm15Status
    allowed_length: int
    error_indicator: int
    edcp_extension: int
    seed: int

    @property
    def error_code(self) -> int:
        """Profile-friendly combined diagnostic value."""

        return self.error_indicator | (self.edcp_extension << 24)


def encode_dm15(
    source_address: int,
    destination_address: int,
    status: Dm15Status | int,
    *,
    allowed_length: int = 0,
    error_indicator: int = 0,
    edcp_extension: int = 0,
    seed: int = 0xFFFF,
) -> CanFrame:
    try:
        status_value = Dm15Status(status)
    except ValueError as exc:
        raise ConfigurationError("invalid DM15 status") from exc
    if not 0 <= allowed_length <= MAX_DM16_DATA:
        raise ConfigurationError(f"DM15 allowed length must be 0..{MAX_DM16_DATA}")
    _require_u24(error_indicator, "DM15 error indicator")
    _require_byte(edcp_extension, "DM15 EDCP extension")
    if not 0 <= seed <= 0xFFFF:
        raise ConfigurationError("DM15 seed must be an unsigned 16-bit value")
    second = (((allowed_length >> 8) & 0x07) << 5) | 0x10
    second |= int(status_value) << 1
    second |= 0x01
    payload = bytes((allowed_length & 0xFF, second))
    payload += error_indicator.to_bytes(3, "little")
    payload += bytes((edcp_extension,))
    payload += seed.to_bytes(2, "little")
    return _make_directed_frame(PGN_DM15, source_address, destination_address, payload)


def decode_dm15(frame: CanFrame) -> Dm15:
    identifier = _directed_identifier(frame, PGN_DM15)
    if frame.dlc != 8:
        raise ProtocolError("DM15 must contain exactly 8 bytes")
    if (frame.data[1] & 0x11) != 0x11:
        raise ProtocolError("DM15 reserved bits must be one")
    allowed_length = frame.data[0] | ((frame.data[1] >> 5) << 8)
    if allowed_length > MAX_DM16_DATA:
        raise ProtocolError(
            f"DM15 allowed length exceeds profile limit {MAX_DM16_DATA}"
        )
    try:
        status = Dm15Status((frame.data[1] >> 1) & 0x07)
    except ValueError as exc:
        raise ProtocolError("unknown DM15 status") from exc
    return Dm15(
        source_address=identifier.source_address,
        destination_address=identifier.destination_address,
        status=status,
        allowed_length=allowed_length,
        error_indicator=int.from_bytes(frame.data[2:5], "little"),
        edcp_extension=frame.data[5],
        seed=int.from_bytes(frame.data[6:8], "little"),
    )


@dataclass(frozen=True, slots=True)
class Dm16:
    source_address: int
    destination_address: int
    data: bytes


def encode_dm16(
    source_address: int,
    destination_address: int,
    data: bytes | bytearray | memoryview,
) -> CanFrame:
    payload = bytes(data)
    if not 1 <= len(payload) <= 7:
        raise ConfigurationError("single-frame DM16 data must contain 1..7 bytes")
    wire = bytes((len(payload),)) + payload
    return _make_directed_frame(
        PGN_DM16,
        source_address,
        destination_address,
        wire.ljust(8, b"\xFF"),
    )


def decode_dm16(frame: CanFrame) -> Dm16:
    identifier = _directed_identifier(frame, PGN_DM16)
    if frame.dlc != 8:
        raise ProtocolError("single-frame DM16 must contain exactly 8 bytes")
    count = frame.data[0]
    if not 1 <= count <= 7:
        raise ProtocolError("single-frame DM16 byte count must be 1..7")
    return Dm16(
        source_address=identifier.source_address,
        destination_address=identifier.destination_address,
        data=frame.data[1 : 1 + count],
    )


def encode_dm16_transport_payload(
    data: bytes | bytearray | memoryview,
) -> bytes:
    payload = bytes(data)
    if not 8 <= len(payload) <= MAX_DM16_DATA:
        raise ConfigurationError(
            f"multipacket DM16 data must contain 8..{MAX_DM16_DATA} bytes"
        )
    return b"\xFF" + payload


def decode_dm16_transport_payload(payload: bytes | bytearray | memoryview) -> bytes:
    data = bytes(payload)
    if not 9 <= len(data) <= MAX_DM16_DATA + 1 or data[0] != 0xFF:
        raise ProtocolError("invalid multipacket DM16 payload")
    return data[1:]


def encode_commanded_address_payload(
    name: J1939Name | int, new_address: int
) -> bytes:
    if not 0 <= new_address <= 0xFD:
        raise ConfigurationError("commanded address must be between 0x00 and 0xFD")
    target_name = name if isinstance(name, J1939Name) else J1939Name(name)
    return target_name.to_bytes() + bytes((new_address,))


def decode_commanded_address_payload(
    payload: bytes | bytearray | memoryview,
) -> tuple[J1939Name, int]:
    data = bytes(payload)
    if len(data) != 9:
        raise ProtocolError("Commanded Address payload must contain exactly 9 bytes")
    return J1939Name.from_bytes(data[:8]), data[8]


__all__ = [
    "CanFrame",
    "ControlRequest",
    "ControlResponse",
    "Dm14",
    "Dm15",
    "Dm16",
    "decode_commanded_address_payload",
    "decode_control_request",
    "decode_dm14",
    "decode_dm15",
    "decode_dm16",
    "decode_dm16_transport_payload",
    "decode_response",
    "encode_commanded_address_payload",
    "encode_control_request",
    "encode_dm14",
    "encode_dm15",
    "encode_dm16",
    "encode_dm16_transport_payload",
    "encode_response",
]
