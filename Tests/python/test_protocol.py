import pytest

from canloader.constants import (
    MAX_DM16_DATA,
    Command,
    Dm14Command,
    Dm15Status,
    PointerType,
    ResponseKind,
)
from canloader.crc import crc32_iso_hdlc
from canloader.errors import ConfigurationError, ProtocolError
from canloader.protocol import (
    CanFrame,
    decode_commanded_address_payload,
    decode_control_request,
    decode_dm14,
    decode_dm15,
    decode_dm16,
    decode_dm16_transport_payload,
    decode_response,
    encode_commanded_address_payload,
    encode_control_request,
    encode_dm14,
    encode_dm15,
    encode_dm16,
    encode_dm16_transport_payload,
    encode_response,
)


def test_proprietary_a_request_and_response_match_v2_byte_contract() -> None:
    request_frame = encode_control_request(
        0xF9,
        0x80,
        Command.INFO,
        0x2A,
        parameter=7,
        value=0x12345678,
    )
    assert request_frame.arbitration_id == 0x18EF80F9
    assert request_frame.data == bytes.fromhex("02 02 2a 07 78 56 34 12")
    request = decode_control_request(request_frame)
    assert (request.source_address, request.destination_address) == (0xF9, 0x80)
    assert request.parameter == 7
    assert request.value == 0x12345678

    response_frame = encode_response(
        0x80, 0xF9, ResponseKind.ACK, Command.INFO, 0x2A, 0x89ABCDEF
    )
    assert response_frame.arbitration_id == 0x18EFF980
    assert response_frame.data == bytes.fromhex("02 80 02 2a ef cd ab 89")
    response = decode_response(response_frame)
    assert response.kind is ResponseKind.ACK
    assert response.value == 0x89ABCDEF


def test_dm14_layout_matches_embedded_contract_byte_for_byte() -> None:
    frame = encode_dm14(
        0xF9,
        0x80,
        Dm14Command.WRITE,
        length=0x364,
        pointer=0x12345678,
        key=0x3412,
        pointer_type=PointerType.INDIRECT,
    )
    assert frame.arbitration_id == 0x18D980F9
    assert frame.data == bytes.fromhex("64 75 78 56 34 12 12 34")
    decoded = decode_dm14(frame)
    assert decoded.command is Dm14Command.WRITE
    assert decoded.pointer_type is PointerType.INDIRECT
    assert decoded.length == 0x364
    assert decoded.pointer == 0x12345678
    assert decoded.key == 0x3412


def test_dm14_reserved_bit_and_profile_length_are_enforced() -> None:
    good = encode_dm14(
        0xF9, 0x80, Dm14Command.WRITE, length=100, pointer=0x08005000
    )
    assert good.data == bytes.fromhex("64 05 00 50 00 08 ff ff")
    bad = CanFrame(good.arbitration_id, bytes((good.data[0], good.data[1] & 0xFE)) + good.data[2:])
    with pytest.raises(ProtocolError, match="reserved bit"):
        decode_dm14(bad)
    with pytest.raises(ConfigurationError, match=str(MAX_DM16_DATA)):
        encode_dm14(
            0xF9,
            0x80,
            Dm14Command.WRITE,
            length=MAX_DM16_DATA + 1,
            pointer=0,
        )


def test_dm15_layout_uses_status_values_four_and_five_and_u24_error() -> None:
    frame = encode_dm15(
        0x80,
        0xF9,
        Dm15Status.OPERATION_COMPLETED,
        allowed_length=0x364,
        error_indicator=0x563412,
        edcp_extension=0x78,
        seed=0xBC9A,
    )
    assert frame.arbitration_id == 0x18D8F980
    assert frame.data == bytes.fromhex("64 79 12 34 56 78 9a bc")
    response = decode_dm15(frame)
    assert response.status is Dm15Status.OPERATION_COMPLETED
    assert response.allowed_length == 0x364
    assert response.error_indicator == 0x563412
    assert response.edcp_extension == 0x78
    assert response.error_code == 0x78563412
    assert response.seed == 0xBC9A
    assert encode_dm15(0x80, 0xF9, Dm15Status.OPERATION_FAILED).data[1] == 0x1B


def test_dm15_requires_both_fixed_reserved_bits() -> None:
    frame = encode_dm15(0x80, 0xF9, Dm15Status.PROCEED, allowed_length=100)
    assert frame.data[1] == 0x11
    for mask in (0x01, 0x10):
        damaged = CanFrame(
            frame.arbitration_id,
            frame.data[:1] + bytes((frame.data[1] & ~mask,)) + frame.data[2:],
        )
        with pytest.raises(ProtocolError, match="reserved bits"):
            decode_dm15(damaged)


def test_dm16_single_and_transport_encodings_are_distinct() -> None:
    direct = encode_dm16(0xF9, 0x80, b"abcdefg")
    assert direct.arbitration_id == 0x18D780F9
    assert direct.data == b"\x07abcdefg"
    assert decode_dm16(direct).data == b"abcdefg"

    raw = bytes(range(100))
    transported = encode_dm16_transport_payload(raw)
    assert transported == b"\xFF" + raw
    assert decode_dm16_transport_payload(transported) == raw
    with pytest.raises(ConfigurationError):
        encode_dm16_transport_payload(b"1234567")


def test_commanded_address_is_exact_name_le64_plus_address() -> None:
    payload = encode_commanded_address_payload(0x0123456789ABCDEF, 0x92)
    assert payload == bytes.fromhex("ef cd ab 89 67 45 23 01 92")
    name, address = decode_commanded_address_payload(payload)
    assert name.value == 0x0123456789ABCDEF
    assert address == 0x92


def test_invalid_control_dlc_and_crc_known_vector() -> None:
    with pytest.raises(ProtocolError, match="exactly 8"):
        decode_response(CanFrame(0x18EFF980, b"\x02\x80"))
    with pytest.raises(ConfigurationError, match="destination address"):
        encode_control_request(0xF9, 0xFF, Command.IDENTIFY, 1)
    assert crc32_iso_hdlc(b"123456789") == 0xCBF43926
