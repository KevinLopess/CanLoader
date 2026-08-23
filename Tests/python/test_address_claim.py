import pytest
from types import SimpleNamespace

import canloader.address_claim as address_claim_module
from _fakes import FakeBus
from canloader.address_claim import (
    AddressBook,
    AddressClaimManager,
    encode_address_claim,
    encode_request_for_address_claimed,
)
from canloader.constants import (
    GLOBAL_ADDRESS,
    NULL_ADDRESS,
    PGN_ADDRESS_CLAIMED,
    PGN_REQUEST,
)
from canloader.errors import AddressClaimError, AddressConflictError
from canloader.j1939 import J1939Name, identifier_from_frame
from canloader.j1939 import make_frame


def test_address_claim_and_request_frames_match_standard_pgns() -> None:
    name = J1939Name(0x8123456789ABCDEF)
    claim = encode_address_claim(0x80, name)
    claim_id = identifier_from_frame(claim)
    assert claim_id.pgn == PGN_ADDRESS_CLAIMED
    assert claim_id.destination_address == GLOBAL_ADDRESS
    assert claim.arbitration_id == 0x18EEFF80
    assert claim.data == name.to_bytes()

    request = encode_request_for_address_claimed(0xF9)
    request_id = identifier_from_frame(request)
    assert request_id.pgn == PGN_REQUEST
    assert request_id.destination_address == GLOBAL_ADDRESS
    assert request.data == bytes.fromhex("00 ee 00 ff ff ff ff ff")


def test_address_book_tracks_moves_and_does_not_hide_conflicts() -> None:
    book = AddressBook()
    first = J1939Name(1)
    second = J1939Name(2)
    book.observe(first, 0x80, observed_at=1.0)
    book.observe(second, 0x80, observed_at=2.0)
    with pytest.raises(AddressConflictError, match="contested"):
        book.resolve_address(0x80)
    book.observe(second, 0x81, observed_at=3.0)
    assert book.resolve_address(0x80).name == first
    assert book.by_name(second).address == 0x81  # type: ignore[union-attr]


def test_lower_numeric_name_wins_and_host_uses_aac_fallback() -> None:
    host_name = J1939Name(0x8000000000000100)
    competitor = J1939Name(0x8000000000000001)
    injected = False

    def responder(frame, bus: FakeBus) -> None:
        nonlocal injected
        identifier = identifier_from_frame(frame)
        if (
            not injected
            and identifier.pgn == PGN_ADDRESS_CLAIMED
            and identifier.source_address == 0xF9
        ):
            injected = True
            bus.queue(encode_address_claim(0xF9, competitor))

    bus = FakeBus(responder)
    manager = AddressClaimManager(
        bus,
        name=host_name,
        preferred_address=0xF9,
        claim_timeout=0.001,
        fallback_addresses=(0x80, 0x81),
    )
    assert manager.claim() == 0x80
    assert [identifier_from_frame(frame).source_address for frame in bus.sent] == [
        0xF9,
        0x80,
    ]


def test_host_reasserts_when_its_lower_name_wins() -> None:
    host_name = J1939Name(1)
    competitor = J1939Name(2)
    injected = False

    def responder(frame, bus: FakeBus) -> None:
        nonlocal injected
        identifier = identifier_from_frame(frame)
        if not injected and identifier.pgn == PGN_ADDRESS_CLAIMED:
            injected = True
            bus.queue(encode_address_claim(0xF9, competitor))

    bus = FakeBus(responder)
    manager = AddressClaimManager(
        bus, name=host_name, preferred_address=0xF9, claim_timeout=0.001
    )
    assert manager.claim() == 0xF9
    assert len(bus.sent) == 2


def test_reassertion_restarts_the_full_claim_window(monkeypatch) -> None:
    now = [0.0]

    class TimedBus:
        def __init__(self) -> None:
            self.sent = []
            self.recv_timeouts: list[float] = []
            self.injected = False

        def send(self, frame) -> None:
            self.sent.append(frame)

        def recv(self, timeout: float):
            self.recv_timeouts.append(timeout)
            if not self.injected:
                self.injected = True
                now[0] += 0.2
                return encode_address_claim(0xF9, 2)
            now[0] += timeout
            return None

    monkeypatch.setattr(
        address_claim_module,
        "time",
        SimpleNamespace(monotonic=lambda: now[0]),
    )
    bus = TimedBus()
    manager = AddressClaimManager(
        bus,  # type: ignore[arg-type]
        name=1,
        preferred_address=0xF9,
        claim_timeout=0.25,
    )

    assert manager.claim() == 0xF9
    assert len(bus.sent) == 2
    assert bus.recv_timeouts[0] == pytest.approx(0.25)
    assert bus.recv_timeouts[1] == pytest.approx(0.25)


def test_non_address_capable_name_sends_cannot_claim_after_loss() -> None:
    host_name = J1939Name.from_fields(
        identity_number=2,
        manufacturer_code=0,
        arbitrary_address_capable=False,
    )
    competitor = J1939Name(1)
    injected = False

    def responder(frame, bus: FakeBus) -> None:
        nonlocal injected
        identifier = identifier_from_frame(frame)
        if not injected and identifier.source_address == 0x90:
            injected = True
            bus.queue(encode_address_claim(0x90, competitor))

    bus = FakeBus(responder)
    manager = AddressClaimManager(
        bus, name=host_name, preferred_address=0x90, claim_timeout=0.001
    )
    with pytest.raises(AddressClaimError, match="not address capable"):
        manager.claim()
    assert identifier_from_frame(bus.sent[-1]).source_address == NULL_ADDRESS
    with pytest.raises(AddressClaimError, match="Cannot Claim state"):
        manager.claim()


def test_request_collects_multiple_names_and_filters_unrelated_frames() -> None:
    names = (J1939Name(0x8000000000000002), J1939Name(0x8000000000000003))

    def responder(frame, bus: FakeBus) -> None:
        identifier = identifier_from_frame(frame)
        if identifier.pgn == PGN_REQUEST:
            bus.queue(encode_address_claim(0x80, names[0]))
            bus.queue(encode_address_claim(0x81, names[1]))

    manager = AddressClaimManager(
        FakeBus(responder),
        name=0x8000000000000001,
        preferred_address=0xF9,
        claim_timeout=0.001,
    )
    book = manager.request_claims(0.001)
    assert book.by_name(names[0]).address == 0x80  # type: ignore[union-attr]
    assert book.by_name(names[1]).address == 0x81  # type: ignore[union-attr]


def test_operational_host_reasserts_against_higher_name() -> None:
    bus = FakeBus()
    manager = AddressClaimManager(
        bus, name=1, preferred_address=0xF9, claim_timeout=0.001
    )
    manager.claim()
    before = len(bus.sent)
    manager.process_claim(encode_address_claim(0xF9, 2))
    assert len(bus.sent) == before + 1
    assert bus.sent[-1] == encode_address_claim(0xF9, 1)


def test_operational_aac_host_moves_after_losing_its_address() -> None:
    bus = FakeBus()
    manager = AddressClaimManager(
        bus,
        name=0xA000000000000002,
        preferred_address=0xF9,
        claim_timeout=0.001,
        fallback_addresses=(0x81,),
    )
    manager.claim()

    with pytest.raises(AddressClaimError, match="won by lower NAME"):
        manager.process_claim(
            encode_address_claim(0xF9, 0x8000000000000001)
        )

    assert manager.operational is False
    assert manager.address == 0x81
    assert manager.claim() == 0x81
    assert bus.sent[-1] == encode_address_claim(0x81, manager.name)


@pytest.mark.parametrize("destination", [GLOBAL_ADDRESS, 0xF9])
def test_operational_host_answers_global_and_directed_address_requests(
    destination: int,
) -> None:
    bus = FakeBus()
    manager = AddressClaimManager(
        bus, name=1, preferred_address=0xF9, claim_timeout=0.001
    )
    manager.claim()
    before = len(bus.sent)
    request = make_frame(
        PGN_REQUEST,
        0x80,
        PGN_ADDRESS_CLAIMED.to_bytes(3, "little") + b"\xFF" * 5,
        destination_address=destination,
    )
    manager.process_claim(request)
    assert len(bus.sent) == before + 1
    assert bus.sent[-1] == encode_address_claim(0xF9, 1)


def test_operational_host_ignores_request_for_another_pgn() -> None:
    bus = FakeBus()
    manager = AddressClaimManager(
        bus, name=1, preferred_address=0xF9, claim_timeout=0.001
    )
    manager.claim()
    before = len(bus.sent)
    request = make_frame(
        PGN_REQUEST,
        0x80,
        (0xF004).to_bytes(3, "little") + b"\xFF" * 5,
        destination_address=GLOBAL_ADDRESS,
    )
    manager.process_claim(request)
    assert len(bus.sent) == before
