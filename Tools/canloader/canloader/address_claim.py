"""J1939 NAME/address discovery and source-address claiming."""

from dataclasses import dataclass
import logging
import math
import time

from .bus import CanBus
from .constants import (
    AAC_FALLBACK_ADDRESSES,
    ADDRESS_CLAIM_PRIORITY,
    ADDRESS_CLAIM_WAIT,
    GLOBAL_ADDRESS,
    NULL_ADDRESS,
    PGN_ADDRESS_CLAIMED,
    PGN_REQUEST,
)
from .errors import (
    AddressClaimError,
    AddressConflictError,
    CanBusError,
    ConfigurationError,
    ProtocolError,
)
from .j1939 import (
    CanFrame,
    J1939Name,
    identifier_from_frame,
    make_frame,
    validate_unicast_address,
)


LOGGER = logging.getLogger(__name__)


@dataclass(frozen=True, slots=True)
class AddressClaim:
    name: J1939Name
    address: int
    observed_at: float


class AddressBook:
    """Tracks NAME-to-address claims without hiding address conflicts."""

    def __init__(self) -> None:
        self._by_name: dict[int, AddressClaim] = {}
        self._by_address: dict[int, set[int]] = {}

    def observe(
        self,
        name: J1939Name | int,
        address: int,
        *,
        observed_at: float | None = None,
    ) -> AddressClaim:
        claimed_name = name if isinstance(name, J1939Name) else J1939Name(name)
        if not 0 <= address <= NULL_ADDRESS:
            raise ConfigurationError("claimed source address must be 0x00..0xFE")
        previous = self._by_name.get(claimed_name.value)
        if previous is not None and previous.address != NULL_ADDRESS:
            names = self._by_address.get(previous.address)
            if names is not None:
                names.discard(claimed_name.value)
                if not names:
                    self._by_address.pop(previous.address, None)

        claim = AddressClaim(
            claimed_name,
            address,
            time.monotonic() if observed_at is None else observed_at,
        )
        self._by_name[claimed_name.value] = claim
        if address != NULL_ADDRESS:
            self._by_address.setdefault(address, set()).add(claimed_name.value)
        return claim

    def observe_frame(self, frame: CanFrame) -> AddressClaim | None:
        try:
            identifier = identifier_from_frame(frame)
        except ProtocolError:
            return None
        if (
            identifier.pgn != PGN_ADDRESS_CLAIMED
            or identifier.destination_address != GLOBAL_ADDRESS
            or frame.dlc != 8
        ):
            return None
        try:
            name = J1939Name.from_bytes(frame.data)
        except ProtocolError:
            return None
        return self.observe(name, identifier.source_address)

    def by_name(self, name: J1939Name | int) -> AddressClaim | None:
        value = name.value if isinstance(name, J1939Name) else name
        return self._by_name.get(value)

    def at_address(self, address: int) -> tuple[AddressClaim, ...]:
        names = self._by_address.get(address, set())
        return tuple(self._by_name[name] for name in sorted(names))

    def resolve_address(self, address: int) -> AddressClaim:
        claims = self.at_address(address)
        if not claims:
            raise AddressClaimError(
                f"no J1939 NAME claimed address 0x{address:02X}"
            )
        if len(claims) != 1:
            values = ", ".join(str(claim.name) for claim in claims)
            raise AddressConflictError(
                f"address 0x{address:02X} is contested by {values}"
            )
        return claims[0]

    def all(self, *, include_cannot_claim: bool = True) -> tuple[AddressClaim, ...]:
        claims = self._by_name.values()
        if not include_cannot_claim:
            claims = (claim for claim in claims if claim.address != NULL_ADDRESS)
        return tuple(sorted(claims, key=lambda claim: (claim.address, claim.name.value)))

    def occupied_addresses(self) -> set[int]:
        return set(self._by_address)


def encode_address_claim(address: int, name: J1939Name | int) -> CanFrame:
    if address != NULL_ADDRESS:
        validate_unicast_address(address, "claimed address")
    claimed_name = name if isinstance(name, J1939Name) else J1939Name(name)
    return make_frame(
        PGN_ADDRESS_CLAIMED,
        address,
        claimed_name.to_bytes(),
        destination_address=GLOBAL_ADDRESS,
        priority=ADDRESS_CLAIM_PRIORITY,
    )


def encode_request_for_address_claimed(source_address: int) -> CanFrame:
    requested_pgn = PGN_ADDRESS_CLAIMED.to_bytes(3, "little")
    return make_frame(
        PGN_REQUEST,
        source_address,
        requested_pgn + b"\xFF" * 5,
        destination_address=GLOBAL_ADDRESS,
    )


class AddressClaimManager:
    """Claims a source address and maintains an AddressBook.

    The manager is synchronous by design so it works identically with
    SocketCAN, SLCAN and virtual python-can backends.
    """

    def __init__(
        self,
        bus: CanBus,
        *,
        name: J1939Name | int,
        preferred_address: int,
        claim_timeout: float = ADDRESS_CLAIM_WAIT,
        fallback_addresses: tuple[int, ...] = AAC_FALLBACK_ADDRESSES,
        address_book: AddressBook | None = None,
    ) -> None:
        validate_unicast_address(preferred_address, "preferred address")
        if not math.isfinite(claim_timeout) or claim_timeout <= 0:
            raise ConfigurationError("address-claim timeout must be greater than zero")
        for address in fallback_addresses:
            validate_unicast_address(address, "fallback address")
        self.bus = bus
        self.name = name if isinstance(name, J1939Name) else J1939Name(name)
        self.preferred_address = preferred_address
        self.address = preferred_address
        self.claim_timeout = claim_timeout
        self.fallback_addresses = tuple(dict.fromkeys(fallback_addresses))
        self.address_book = address_book or AddressBook()
        self.operational = False

    def _send(self, frame: CanFrame) -> None:
        LOGGER.debug(
            "TX %08X [%d] %s",
            frame.arbitration_id,
            frame.dlc,
            frame.data.hex(" "),
        )
        try:
            self.bus.send(frame)
        except CanBusError:
            raise
        except Exception as exc:
            raise CanBusError(f"failed to send CAN frame: {exc}") from exc

    def _recv(self, timeout: float) -> CanFrame | None:
        try:
            frame = self.bus.recv(timeout)
        except CanBusError:
            raise
        except Exception as exc:
            raise CanBusError(f"failed to receive CAN frame: {exc}") from exc
        if frame is not None:
            LOGGER.debug(
                "RX %08X [%d] %s",
                frame.arbitration_id,
                frame.dlc,
                frame.data.hex(" "),
            )
        return frame

    def _next_free_fallback(self, attempted: set[int]) -> int | None:
        occupied = self.address_book.occupied_addresses()
        for candidate in self.fallback_addresses:
            if candidate not in attempted and candidate not in occupied:
                return candidate
        return None

    def claim(self) -> int:
        if self.operational:
            return self.address
        if self.address == NULL_ADDRESS:
            raise AddressClaimError(
                "source NAME is in Cannot Claim state; no unicast address is available"
            )

        attempted: set[int] = set()
        candidate = self.address
        while True:
            attempted.add(candidate)
            self.address = candidate
            self.address_book.observe(self.name, candidate)
            self._send(encode_address_claim(candidate, self.name))
            deadline = time.monotonic() + self.claim_timeout
            lost = False
            while True:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    break
                frame = self._recv(remaining)
                if frame is None:
                    break
                claim = self.address_book.observe_frame(frame)
                if claim is None or claim.address != candidate:
                    continue
                if claim.name.value == self.name.value:
                    continue
                if claim.name.value < self.name.value:
                    lost = True
                    break
                # The lower numeric NAME wins. Reassert so the loser sees it.
                self._send(encode_address_claim(candidate, self.name))
                deadline = time.monotonic() + self.claim_timeout

            if not lost:
                self.operational = True
                return candidate
            if not self.name.arbitrary_address_capable:
                self._send(encode_address_claim(NULL_ADDRESS, self.name))
                self.address = NULL_ADDRESS
                raise AddressClaimError(
                    "source NAME lost address arbitration and is not address capable"
                )
            replacement = self._next_free_fallback(attempted)
            if replacement is None:
                self._send(encode_address_claim(NULL_ADDRESS, self.name))
                self.address = NULL_ADDRESS
                raise AddressClaimError("no free fallback J1939 source address")
            candidate = replacement

    def process_claim(self, frame: CanFrame) -> AddressClaim | None:
        try:
            identifier = identifier_from_frame(frame)
        except ProtocolError:
            return None
        if (
            self.operational
            and identifier.pgn == PGN_REQUEST
            and identifier.destination_address in (GLOBAL_ADDRESS, self.address)
            and frame.dlc >= 3
            and int.from_bytes(frame.data[:3], "little") == PGN_ADDRESS_CLAIMED
        ):
            self._send(encode_address_claim(self.address, self.name))
            return None

        claim = self.address_book.observe_frame(frame)
        if (
            claim is not None
            and self.operational
            and claim.address == self.address
            and claim.name.value != self.name.value
        ):
            if claim.name.value < self.name.value:
                lost_address = self.address
                self.operational = False
                if not self.name.arbitrary_address_capable:
                    self.address = NULL_ADDRESS
                    self._send(encode_address_claim(NULL_ADDRESS, self.name))
                else:
                    replacement = self._next_free_fallback({lost_address})
                    if replacement is None:
                        self.address = NULL_ADDRESS
                        self._send(encode_address_claim(NULL_ADDRESS, self.name))
                    else:
                        self.address = replacement
                raise AddressClaimError(
                    f"source address 0x{lost_address:02X} was won by lower "
                    f"NAME {claim.name}"
                )
            # Our lower numeric NAME wins; the competing CA must observe the
            # reannouncement and move or Cannot Claim.
            self._send(encode_address_claim(self.address, self.name))
        return claim

    def request_claims(self, timeout: float) -> AddressBook:
        if not math.isfinite(timeout) or timeout <= 0:
            raise ConfigurationError("scan timeout must be greater than zero")
        self.claim()
        self._send(encode_request_for_address_claimed(self.address))
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            frame = self._recv(remaining)
            if frame is None:
                break
            self.process_claim(frame)
        return self.address_book
