"""Encoding and decoding for version 1 of the CanLoader wire protocol."""

from dataclasses import dataclass

from .constants import (
    COMMAND_ID_BASE,
    DATA_ID_BASE,
    MAX_BLOCK_SIZE,
    MAX_DATA_PAYLOAD,
    MAX_NODE_ID,
    MIN_NODE_ID,
    PROTOCOL_VERSION,
    RESPONSE_ID_BASE,
    Command,
    ResponseKind,
)
from .errors import ConfigurationError, ProtocolError


def _require_byte(value: int, name: str, *, nonzero: bool = False) -> None:
    minimum = 1 if nonzero else 0
    if not minimum <= value <= 0xFF:
        qualifier = "nonzero " if nonzero else ""
        raise ConfigurationError(f"{name} must be a {qualifier}8-bit value")


def validate_node(node: int) -> None:
    if not MIN_NODE_ID <= node <= MAX_NODE_ID:
        raise ConfigurationError(
            f"node must be between {MIN_NODE_ID} and {MAX_NODE_ID}"
        )


def response_id(node: int) -> int:
    validate_node(node)
    return RESPONSE_ID_BASE + node


def command_id(node: int) -> int:
    validate_node(node)
    return COMMAND_ID_BASE + node


def data_id(node: int) -> int:
    validate_node(node)
    return DATA_ID_BASE + node


@dataclass(frozen=True, slots=True)
class CanFrame:
    arbitration_id: int
    data: bytes
    is_extended_id: bool = False

    def __post_init__(self) -> None:
        payload = bytes(self.data)
        maximum_id = 0x1FFFFFFF if self.is_extended_id else 0x7FF
        if not 0 <= self.arbitration_id <= maximum_id:
            raise ProtocolError("CAN arbitration ID is out of range")
        if len(payload) > 8:
            raise ProtocolError("classic CAN payload cannot exceed 8 bytes")
        object.__setattr__(self, "data", payload)

    @property
    def dlc(self) -> int:
        return len(self.data)


@dataclass(frozen=True, slots=True)
class Response:
    version: int
    kind: ResponseKind
    command: int
    session: int
    value: int


def encode_control_request(
    node: int,
    command: Command | int,
    session: int,
    *,
    byte3: int = 0,
    value: int = 0,
) -> CanFrame:
    _require_byte(session, "session", nonzero=True)
    _require_byte(int(command), "command")
    _require_byte(byte3, "byte3")
    if not 0 <= value <= 0xFFFFFFFF:
        raise ConfigurationError("control value must be an unsigned 32-bit value")
    payload = bytes((PROTOCOL_VERSION, int(command), session, byte3))
    payload += value.to_bytes(4, "little")
    return CanFrame(command_id(node), payload)


def encode_begin_block(
    node: int,
    session: int,
    block_index: int,
    length: int,
) -> CanFrame:
    _require_byte(session, "session", nonzero=True)
    if not 0 <= block_index <= 0xFFFF:
        raise ConfigurationError("block index must be an unsigned 16-bit value")
    if not 1 <= length <= MAX_BLOCK_SIZE:
        raise ConfigurationError(f"block length must be between 1 and {MAX_BLOCK_SIZE}")
    block_sequence = block_index & 0xFF
    payload = bytes(
        (
            PROTOCOL_VERSION,
            Command.BEGIN_BLOCK,
            session,
            block_sequence,
            block_index & 0xFF,
            block_index >> 8,
            length,
            0,
        )
    )
    return CanFrame(command_id(node), payload)


def encode_data_frame(
    node: int,
    session: int,
    block_sequence: int,
    frame_sequence: int,
    payload: bytes | bytearray | memoryview,
) -> CanFrame:
    _require_byte(session, "session", nonzero=True)
    _require_byte(block_sequence, "block sequence")
    _require_byte(frame_sequence, "frame sequence")
    chunk = bytes(payload)
    if not 1 <= len(chunk) <= MAX_DATA_PAYLOAD:
        raise ConfigurationError(
            f"DATA payload must contain between 1 and {MAX_DATA_PAYLOAD} bytes"
        )
    return CanFrame(
        data_id(node), bytes((session, block_sequence, frame_sequence)) + chunk
    )


def encode_response(
    node: int,
    kind: ResponseKind | int,
    command: Command | int,
    session: int,
    value: int = 0,
    *,
    version: int = PROTOCOL_VERSION,
) -> CanFrame:
    """Encode a response for protocol tests and simulated devices."""

    _require_byte(version, "version")
    _require_byte(int(kind), "response kind")
    _require_byte(int(command), "command")
    _require_byte(session, "session", nonzero=True)
    if not 0 <= value <= 0xFFFFFFFF:
        raise ConfigurationError("response value must be an unsigned 32-bit value")
    payload = bytes((version, int(kind), int(command), session))
    payload += value.to_bytes(4, "little")
    return CanFrame(response_id(node), payload)


def decode_response(frame: CanFrame) -> Response:
    if frame.is_extended_id:
        raise ProtocolError("response must use a standard 11-bit CAN ID")
    if frame.dlc != 8:
        raise ProtocolError("response must contain exactly 8 bytes")
    try:
        kind = ResponseKind(frame.data[1])
    except ValueError as exc:
        raise ProtocolError(f"unknown response kind 0x{frame.data[1]:02X}") from exc
    return Response(
        version=frame.data[0],
        kind=kind,
        command=frame.data[2],
        session=frame.data[3],
        value=int.from_bytes(frame.data[4:8], "little"),
    )
