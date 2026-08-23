import pytest

from _fakes import FakeBus
from canloader.constants import (
    GLOBAL_ADDRESS,
    MAX_TP_PAYLOAD,
    PGN_COMMANDED_ADDRESS,
    PGN_DM16,
    PGN_TP_CM,
    PGN_TP_DT,
    TpControl,
)
from canloader.errors import ConfigurationError, TransportAbortError, TransportError
from canloader.j1939 import identifier_from_frame
from canloader.transport import (
    J1939Transport,
    decode_tp_cm,
    encode_tp_abort,
    encode_tp_cts,
    encode_tp_eom_ack,
)


def test_bam_for_commanded_address_has_exact_cm_and_two_dt_frames() -> None:
    sleeps: list[float] = []
    bus = FakeBus()
    transport = J1939Transport(
        bus,
        source_address=0xF9,
        timeout=0.001,
        bam_interval=0.05,
        sleep=sleeps.append,
    )
    payload = bytes.fromhex("ef cd ab 89 67 45 23 01 92")
    transport.send_bam(PGN_COMMANDED_ADDRESS, payload)

    assert len(bus.sent) == 3
    cm_id = identifier_from_frame(bus.sent[0])
    assert (cm_id.pgn, cm_id.destination_address) == (PGN_TP_CM, GLOBAL_ADDRESS)
    assert bus.sent[0].data == bytes.fromhex("20 09 00 02 ff d8 fe 00")
    assert bus.sent[1].arbitration_id == 0x18EBFFF9
    assert bus.sent[1].data == b"\x01" + payload[:7]
    assert bus.sent[2].data == b"\x02" + payload[7:].ljust(7, b"\xFF")
    assert sleeps == [0.05, 0.05]


def test_rts_cts_transfer_obeys_requested_window_and_waits_for_eom_ack() -> None:
    pgn = PGN_DM16
    payload = b"\xFF" + bytes(range(20))
    dt_seen = 0

    def responder(frame, bus: FakeBus) -> None:
        nonlocal dt_seen
        identifier = identifier_from_frame(frame)
        if identifier.pgn == PGN_TP_CM:
            _, cm = decode_tp_cm(frame)
            if cm.control is TpControl.RTS:
                bus.queue(encode_tp_cts(0x80, 0xF9, pgn, 2, 1))
        elif identifier.pgn == PGN_TP_DT:
            dt_seen += 1
            if dt_seen == 2:
                bus.queue(encode_tp_cts(0x80, 0xF9, pgn, 1, 3))
            elif dt_seen == 3:
                bus.queue(encode_tp_eom_ack(0x80, 0xF9, pgn, len(payload), 3))

    bus = FakeBus(responder)
    transport = J1939Transport(bus, source_address=0xF9, timeout=0.001)
    transport.send_rts_cts(pgn, 0x80, payload)

    controls = []
    sequences = []
    for frame in bus.sent:
        identifier = identifier_from_frame(frame)
        if identifier.pgn == PGN_TP_CM:
            controls.append(decode_tp_cm(frame)[1].control)
        elif identifier.pgn == PGN_TP_DT:
            sequences.append(frame.data[0])
    assert controls == [TpControl.RTS]
    assert sequences == [1, 2, 3]


def test_peer_abort_is_reported_without_sending_a_second_abort() -> None:
    def responder(frame, bus: FakeBus) -> None:
        identifier = identifier_from_frame(frame)
        if identifier.pgn == PGN_TP_CM:
            _, cm = decode_tp_cm(frame)
            if cm.control is TpControl.RTS:
                bus.queue(encode_tp_abort(0x80, 0xF9, PGN_DM16, 3))

    bus = FakeBus(responder)
    transport = J1939Transport(bus, source_address=0xF9, timeout=0.001)
    with pytest.raises(TransportAbortError) as caught:
        transport.send_rts_cts(PGN_DM16, 0x80, b"\xFF12345678")
    assert caught.value.reason == 3
    assert len(bus.sent) == 1


def test_timeout_sends_best_effort_tp_abort() -> None:
    bus = FakeBus()
    transport = J1939Transport(bus, source_address=0xF9, timeout=0.001)
    with pytest.raises(TransportError, match="timed out"):
        transport.send_rts_cts(PGN_DM16, 0x80, b"\xFF12345678")
    assert decode_tp_cm(bus.sent[-1])[1].control is TpControl.ABORT


def test_cts_cannot_request_packets_outside_transfer() -> None:
    def responder(frame, bus: FakeBus) -> None:
        identifier = identifier_from_frame(frame)
        if identifier.pgn == PGN_TP_CM:
            _, cm = decode_tp_cm(frame)
            if cm.control is TpControl.RTS:
                bus.queue(encode_tp_cts(0x80, 0xF9, PGN_DM16, 2, 2))

    bus = FakeBus(responder)
    transport = J1939Transport(bus, source_address=0xF9, timeout=0.001)
    with pytest.raises(TransportError, match="outside"):
        transport.send_rts_cts(PGN_DM16, 0x80, b"\xFF12345678")
    assert decode_tp_cm(bus.sent[-1])[1].control is TpControl.ABORT


def test_transport_limits_and_direct_dispatch() -> None:
    bus = FakeBus()
    transport = J1939Transport(bus, source_address=0xF9, timeout=0.001)
    transport.send_message(PGN_DM16, 0x80, b"12345678")
    assert identifier_from_frame(bus.sent[0]).pgn == PGN_DM16
    with pytest.raises(ConfigurationError, match=str(MAX_TP_PAYLOAD)):
        transport.send_bam(PGN_COMMANDED_ADDRESS, b"x" * (MAX_TP_PAYLOAD + 1))
