import pytest

from canloader.constants import Command, NackCode, ResponseKind
from canloader.crc import crc32_iso_hdlc
from canloader.errors import ConfigurationError, ProtocolError
from canloader.protocol import (
    CanFrame,
    command_id,
    data_id,
    decode_response,
    encode_begin_block,
    encode_control_request,
    encode_data_frame,
    encode_response,
    response_id,
)


def test_standard_identifiers_include_node() -> None:
    assert response_id(1) == 0x581
    assert command_id(1) == 0x601
    assert data_id(1) == 0x701


def test_node_range_matches_firmware_configuration() -> None:
    with pytest.raises(ConfigurationError):
        command_id(0)
    with pytest.raises(ConfigurationError):
        command_id(128)


def test_control_request_encodes_little_endian_value() -> None:
    frame = encode_control_request(
        1, Command.START_UPDATE, 0x2A, value=0x12345678
    )
    assert frame.arbitration_id == 0x601
    assert frame.data == bytes((1, 0x10, 0x2A, 0, 0x78, 0x56, 0x34, 0x12))


def test_begin_block_uses_special_layout() -> None:
    frame = encode_begin_block(1, 7, block_index=0x1234, length=100)
    assert frame.data == bytes((1, 0x20, 7, 0x34, 0x34, 0x12, 100, 0))


def test_data_frame_has_no_protocol_version() -> None:
    frame = encode_data_frame(1, 7, 3, 0, b"abcde")
    assert frame.arbitration_id == 0x701
    assert frame.data == bytes((7, 3, 0)) + b"abcde"
    assert frame.dlc == 8


def test_response_decodes_little_endian_value() -> None:
    frame = encode_response(
        1, ResponseKind.ACK, Command.INFO, 9, value=0x89ABCDEF
    )
    response = decode_response(frame)
    assert response.version == 1
    assert response.kind is ResponseKind.ACK
    assert response.command == Command.INFO
    assert response.session == 9
    assert response.value == 0x89ABCDEF


def test_invalid_response_dlc_is_rejected() -> None:
    with pytest.raises(ProtocolError, match="exactly 8 bytes"):
        decode_response(CanFrame(0x581, b"\x01\x80"))


def test_data_payload_bounds_are_checked() -> None:
    with pytest.raises(ConfigurationError):
        encode_data_frame(1, 1, 0, 0, b"")
    with pytest.raises(ConfigurationError):
        encode_data_frame(1, 1, 0, 0, b"123456")


def test_crc32_iso_hdlc_known_vector() -> None:
    assert crc32_iso_hdlc(b"123456789") == 0xCBF43926


def test_nack_codes_match_the_firmware_wire_values() -> None:
    assert [code.value for code in NackCode] == list(range(16))
