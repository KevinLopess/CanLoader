import pytest

from canloader.errors import ConfigurationError, ProtocolError
from canloader.j1939 import (
    CanFrame,
    J1939Identifier,
    J1939Name,
    identifier_from_frame,
    make_frame,
)


def test_pdu1_identifier_uses_ps_as_destination_not_pgn() -> None:
    identifier = J1939Identifier(6, 0xEF00, 0xF9, 0x80)
    assert identifier.encode() == 0x18EF80F9
    assert J1939Identifier.decode(0x18EF80F9) == identifier
    assert identifier.is_pdu1 is True


def test_pdu2_identifier_keeps_group_extension_in_pgn() -> None:
    identifier = J1939Identifier(6, 0xFED8, 0x80)
    assert identifier.encode() == 0x18FED880
    assert J1939Identifier.decode(0x18FED880) == identifier
    assert identifier.destination_address is None


def test_pdu1_rejects_noncanonical_pgn_and_missing_destination() -> None:
    with pytest.raises(ConfigurationError, match="canonical"):
        J1939Identifier(6, 0xEE55, 0x80, 0xFF)
    with pytest.raises(ConfigurationError, match="requires a destination"):
        J1939Identifier(6, 0xEE00, 0x80)


def test_reserved_identifier_bit_and_non_extended_frame_are_rejected() -> None:
    with pytest.raises(ProtocolError, match="reserved"):
        J1939Identifier.decode(0x18EF80F9 | (1 << 25))
    with pytest.raises(ProtocolError, match="29-bit"):
        identifier_from_frame(CanFrame(0x601, b"", is_extended_id=False))
    with pytest.raises(ProtocolError, match="source address"):
        J1939Identifier.decode(0x18EF80FF)


def test_make_frame_is_always_extended_and_checks_classic_can_limit() -> None:
    frame = make_frame(0xEA00, 0xF9, b"abc", destination_address=0xFF)
    assert frame.is_extended_id is True
    assert frame.arbitration_id == 0x18EAFFF9
    with pytest.raises(ProtocolError, match="cannot exceed 8"):
        make_frame(0xEA00, 0xF9, b"123456789", destination_address=0xFF)


def test_name_is_little_endian_and_numeric_order_is_claim_priority() -> None:
    low = J1939Name(0x0123456789ABCDEF)
    high = J1939Name(0x8123456789ABCDEF)
    assert low.to_bytes() == bytes.fromhex("ef cd ab 89 67 45 23 01")
    assert J1939Name.from_bytes(low.to_bytes()) == low
    assert low < high


def test_name_field_builder_round_trips_every_field() -> None:
    name = J1939Name.from_fields(
        identity_number=0x12345,
        manufacturer_code=0x321,
        ecu_instance=5,
        function_instance=17,
        function=0x92,
        vehicle_system=0x55,
        vehicle_system_instance=9,
        industry_group=3,
        arbitrary_address_capable=True,
    )
    assert name.identity_number == 0x12345
    assert name.manufacturer_code == 0x321
    assert name.ecu_instance == 5
    assert name.function_instance == 17
    assert name.function == 0x92
    assert name.vehicle_system == 0x55
    assert name.vehicle_system_instance == 9
    assert name.industry_group == 3
    assert name.arbitrary_address_capable is True


def test_name_requires_exactly_eight_wire_bytes() -> None:
    with pytest.raises(ProtocolError, match="exactly 8"):
        J1939Name.from_bytes(b"short")
