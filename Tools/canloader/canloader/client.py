"""High-level CanLoader client and J1939 network scanner."""

from collections import deque
from collections.abc import Callable
from dataclasses import dataclass
import logging
import math
import secrets
import time

from .address_claim import AddressClaimManager
from .bus import CanBus
from .constants import (
    AAC_FALLBACK_ADDRESSES,
    CAPABILITY_APPLICATION_RUNNING,
    CAPABILITY_APPLICATION_VALID,
    CAPABILITY_UPDATE_ACTIVE,
    DEFAULT_SOURCE_ADDRESS,
    DEFAULT_SOURCE_NAME,
    DEFAULT_TARGET_ADDRESS,
    FLASH_ERASE_BLOCK_SIZE,
    MIN_IMAGE_SIZE,
    NULL_ADDRESS,
    PGN_ADDRESS_CLAIMED,
    PGN_COMMANDED_ADDRESS,
    PGN_DM15,
    PGN_DM16,
    PROTOCOL_VERSION,
    REQUIRED_PROFILE_CAPABILITIES,
    SUPPORTED_TARGET_CODES,
    UPDATE_BLOCK_SIZE,
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
    BlockTransferError,
    CanBusError,
    ConfigurationError,
    DeviceSessionTimeout,
    NackError,
    ProtocolError,
    ResponseTimeout,
    TargetIdentityError,
    TransportError,
    VerificationError,
)
from .j1939 import (
    CanFrame,
    J1939Name,
    identifier_from_frame,
    validate_unicast_address,
)
from .protocol import (
    Dm15,
    decode_dm15,
    decode_response,
    encode_commanded_address_payload,
    encode_control_request,
    encode_dm14,
    encode_dm16,
    encode_dm16_transport_payload,
)
from .transport import J1939Transport


LOGGER = logging.getLogger(__name__)
ProgressCallback = Callable[[int, int], None]
ValuePredicate = Callable[[int], bool]


def _validate_identify_capabilities(
    capabilities: int, expected_address: int
) -> int:
    advertised_address = (capabilities >> 8) & 0xFF
    advertised_version = (capabilities >> 16) & 0xFF
    advertised_target = (capabilities >> 24) & 0xFF
    if advertised_address != expected_address:
        raise ProtocolError(
            f"IDENTIFY advertised address 0x{advertised_address:02X}; "
            f"expected 0x{expected_address:02X}"
        )
    if advertised_version != PROTOCOL_VERSION:
        raise ProtocolError(
            f"IDENTIFY advertised protocol {advertised_version}; "
            f"expected {PROTOCOL_VERSION}"
        )
    if advertised_target not in SUPPORTED_TARGET_CODES:
        supported = ", ".join(
            f"0x{target:02X}" for target in sorted(SUPPORTED_TARGET_CODES)
        )
        raise ProtocolError(
            f"IDENTIFY advertised target code 0x{advertised_target:02X}; "
            f"supported codes are {supported}"
        )
    missing = REQUIRED_PROFILE_CAPABILITIES & ~capabilities
    if missing:
        raise ProtocolError(
            f"IDENTIFY is missing required profile capabilities 0x{missing:02X}"
        )
    return capabilities


@dataclass(frozen=True, slots=True)
class DeviceInfo:
    address: int
    name: J1939Name
    capabilities: int
    app_base: int
    app_max_size: int
    flash_erase_unit_size: int
    stored_size: int
    stored_crc: int
    uid_words: tuple[int, int, int]

    @property
    def uid(self) -> str:
        return "".join(f"{word:08X}" for word in reversed(self.uid_words))


@dataclass(frozen=True, slots=True)
class DiscoveredDevice:
    address: int
    name: J1939Name
    capabilities: int | None = None

    @property
    def is_canloader(self) -> bool:
        return self.capabilities is not None

    @property
    def target_code(self) -> int | None:
        if self.capabilities is None:
            return None
        return (self.capabilities >> 24) & 0xFF

    @property
    def protocol_version(self) -> int | None:
        if self.capabilities is None:
            return None
        return (self.capabilities >> 16) & 0xFF

    @property
    def application_valid(self) -> bool:
        return bool((self.capabilities or 0) & CAPABILITY_APPLICATION_VALID)

    @property
    def update_active(self) -> bool:
        return bool((self.capabilities or 0) & CAPABILITY_UPDATE_ACTIVE)

    @property
    def application_running(self) -> bool:
        return bool((self.capabilities or 0) & CAPABILITY_APPLICATION_RUNNING)


@dataclass(frozen=True, slots=True)
class UpdateResult:
    size: int
    crc: int
    verified_crc: int
    ran_application: bool
    target_name: J1939Name


class _RawEndpoint:
    def __init__(self, bus: CanBus) -> None:
        self.bus = bus

    def send(self, frame: CanFrame) -> None:
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

    def recv(self, timeout: float) -> CanFrame | None:
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


class NetworkScanner(_RawEndpoint):
    """Lists every claiming J1939 NAME and optionally identifies CanLoader nodes."""

    def __init__(
        self,
        bus: CanBus,
        *,
        timeout: float = 0.2,
        identify_timeout: float | None = None,
        source_address: int = DEFAULT_SOURCE_ADDRESS,
        source_name: J1939Name | int = DEFAULT_SOURCE_NAME,
        claim_timeout: float = 0.250,
        session: int | None = None,
    ) -> None:
        super().__init__(bus)
        if not math.isfinite(timeout) or timeout <= 0:
            raise ConfigurationError("scan timeout must be greater than zero")
        if identify_timeout is None:
            identify_timeout = timeout
        if not math.isfinite(identify_timeout) or identify_timeout <= 0:
            raise ConfigurationError("identify timeout must be greater than zero")
        if session is None:
            session = secrets.randbelow(0xFF) + 1
        if not 1 <= session <= 0xFF:
            raise ConfigurationError("session must be between 1 and 255")
        self.timeout = timeout
        self.identify_timeout = identify_timeout
        self._session = session
        self.claim_manager = AddressClaimManager(
            bus,
            name=source_name,
            preferred_address=source_address,
            claim_timeout=claim_timeout,
        )

    def _next_session(self) -> int:
        self._session = (self._session % 0xFF) + 1
        return self._session

    def _identify(self, address: int) -> int | None:
        session = self._next_session()
        frame = encode_control_request(
            self.claim_manager.address,
            address,
            Command.IDENTIFY,
            session,
        )
        self.send(frame)
        deadline = time.monotonic() + self.identify_timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return None
            response_frame = self.recv(remaining)
            if response_frame is None:
                return None
            claim = self.claim_manager.process_claim(response_frame)
            if claim is not None:
                continue
            try:
                response = decode_response(response_frame)
            except ProtocolError:
                continue
            if (
                response.source_address == address
                and response.destination_address == self.claim_manager.address
                and response.version == PROTOCOL_VERSION
                and response.kind is ResponseKind.ACK
                and response.command == int(Command.IDENTIFY)
                and response.session == session
            ):
                try:
                    return _validate_identify_capabilities(response.value, address)
                except ProtocolError:
                    return None

    def scan(self) -> list[DiscoveredDevice]:
        book = self.claim_manager.request_claims(self.timeout)
        own_name = self.claim_manager.name.value
        claims = [
            claim
            for claim in book.all(include_cannot_claim=True)
            if claim.name.value != own_name
        ]
        counts: dict[int, int] = {}
        for claim in claims:
            counts[claim.address] = counts.get(claim.address, 0) + 1

        devices: list[DiscoveredDevice] = []
        for claim in claims:
            capabilities = None
            if claim.address != NULL_ADDRESS and counts[claim.address] == 1:
                capabilities = self._identify(claim.address)
            devices.append(
                DiscoveredDevice(
                    address=claim.address,
                    name=claim.name,
                    capabilities=capabilities,
                )
            )
        return devices


class BootloaderClient(_RawEndpoint):
    def __init__(
        self,
        bus: CanBus,
        *,
        address: int = DEFAULT_TARGET_ADDRESS,
        target_name: J1939Name | int | None = None,
        source_address: int = DEFAULT_SOURCE_ADDRESS,
        source_name: J1939Name | int = DEFAULT_SOURCE_NAME,
        timeout: float = 0.5,
        erase_timeout: float = 4.0,
        claim_timeout: float = 0.250,
        scan_timeout: float = 0.2,
        transport_timeout: float = 1.5,
        retries: int = 3,
        session: int | None = None,
    ) -> None:
        super().__init__(bus)
        validate_unicast_address(address, "target address")
        validate_unicast_address(source_address, "source address")
        if address == source_address:
            raise ConfigurationError("source and target addresses must differ")
        for value, label in (
            (timeout, "timeout"),
            (erase_timeout, "erase timeout"),
            (claim_timeout, "claim timeout"),
            (scan_timeout, "scan timeout"),
            (transport_timeout, "transport timeout"),
        ):
            if not math.isfinite(value) or value <= 0:
                raise ConfigurationError(f"{label} must be greater than zero")
        if retries < 0:
            raise ConfigurationError("retries cannot be negative")
        if session is None:
            session = secrets.randbelow(0xFF) + 1
        if not 1 <= session <= 0xFF:
            raise ConfigurationError("session must be between 1 and 255")

        self.address = address
        self.target_name = (
            target_name
            if isinstance(target_name, J1939Name)
            else (J1939Name(target_name) if target_name is not None else None)
        )
        self.timeout = timeout
        self.erase_timeout = erase_timeout
        self.scan_timeout = scan_timeout
        self.transport_timeout = transport_timeout
        self.retries = retries
        self.session = session
        self._query_session = session
        self._prepared = False
        self._profile_validated = False
        self._deferred_dm15: deque[CanFrame] = deque()
        self.claim_manager = AddressClaimManager(
            bus,
            name=source_name,
            preferred_address=source_address,
            claim_timeout=claim_timeout,
            fallback_addresses=tuple(
                candidate
                for candidate in AAC_FALLBACK_ADDRESSES
                if candidate != address
            ),
        )
        self.transport: J1939Transport | None = None

    @property
    def source_address(self) -> int:
        return self.claim_manager.address

    def _next_query_session(self) -> int:
        self._query_session = (self._query_session % 0xFF) + 1
        if self._query_session == self.session:
            self._query_session = (self._query_session % 0xFF) + 1
        return self._query_session

    def _ensure_source(self) -> None:
        old = self.claim_manager.address
        claimed = self.claim_manager.claim()
        transport_changed = (
            self.transport is not None
            and self.transport.source_address != claimed
        )
        if self.transport is None or transport_changed:
            self.transport = J1939Transport(
                self.bus,
                source_address=claimed,
                timeout=self.transport_timeout,
                frame_observer=self._observe_transport_frame,
            )
        if old != claimed or transport_changed:
            self._prepared = False
            self._profile_validated = False

    def _prepare_target(self, *, force: bool = False) -> J1939Name:
        if self._prepared and not force:
            assert self.target_name is not None
            return self.target_name
        if force:
            self._profile_validated = False
        self._ensure_source()
        book = self.claim_manager.request_claims(self.scan_timeout)
        if self.target_name is None:
            claim = book.resolve_address(self.address)
            self.target_name = claim.name
        else:
            claim = book.by_name(self.target_name)
            if claim is None:
                occupants = book.at_address(self.address)
                if occupants:
                    names = ", ".join(str(item.name) for item in occupants)
                    raise TargetIdentityError(
                        f"address 0x{self.address:02X} is claimed by {names}, "
                        f"not expected NAME {self.target_name}"
                    )
                raise AddressClaimError(
                    f"target NAME {self.target_name} did not claim an address"
                )
            if claim.address != self.address:
                raise TargetIdentityError(
                    f"target NAME {self.target_name} claimed 0x{claim.address:02X}, "
                    f"not requested address 0x{self.address:02X}"
                )
            resolved = book.resolve_address(self.address)
            if resolved.name != self.target_name:
                raise TargetIdentityError(
                    f"address 0x{self.address:02X} belongs to {resolved.name}, "
                    f"not {self.target_name}"
                )
        self._prepared = True
        return self.target_name

    def _observe_frame(self, frame: CanFrame) -> None:
        try:
            claim = self.claim_manager.process_claim(frame)
        except AddressClaimError:
            self._prepared = False
            self._profile_validated = False
            raise
        if claim is None or self.target_name is None:
            return
        if claim.name == self.target_name and claim.address != self.address:
            self._prepared = False
            self._profile_validated = False
            raise TargetIdentityError(
                f"locked target {self.target_name} moved from 0x{self.address:02X} "
                f"to 0x{claim.address:02X} during an operation"
            )
        if claim.address == self.address and claim.name != self.target_name:
            self._prepared = False
            self._profile_validated = False
            raise TargetIdentityError(
                f"address 0x{self.address:02X} was claimed by unexpected NAME {claim.name}"
            )

    def _observe_transport_frame(self, frame: CanFrame) -> None:
        """Preserve application responses encountered while TP waits for CM."""

        self._observe_frame(frame)
        try:
            identifier = identifier_from_frame(frame)
        except ProtocolError:
            return
        if (
            identifier.pgn == PGN_DM15
            and identifier.source_address == self.address
            and identifier.destination_address == self.source_address
        ):
            self._deferred_dm15.append(frame)

    def _wait_for_response(
        self,
        command: Command,
        session: int,
        timeout: float,
        accept_value: ValuePredicate | None = None,
    ) -> int:
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise ResponseTimeout(command)
            frame = self.recv(remaining)
            if frame is None:
                raise ResponseTimeout(command)
            try:
                identifier = identifier_from_frame(frame)
            except ProtocolError:
                continue
            self._observe_frame(frame)
            if identifier.pgn == PGN_ADDRESS_CLAIMED:
                continue
            try:
                response = decode_response(frame)
            except ProtocolError:
                continue
            if (
                response.source_address != self.address
                or response.destination_address != self.source_address
                or response.version != PROTOCOL_VERSION
                or response.session != session
            ):
                continue
            if (
                response.kind is ResponseKind.NACK
                and response.command == int(Command.ABORT)
                and response.value == int(NackCode.TIMEOUT)
            ):
                raise DeviceSessionTimeout(command)
            if response.command != int(command):
                continue
            if response.kind is ResponseKind.NACK:
                raise NackError(command, response.value)
            if accept_value is not None and not accept_value(response.value):
                continue
            return response.value

    def _exchange(
        self,
        frame: CanFrame,
        command: Command,
        session: int,
        *,
        timeout: float | None = None,
        retries: int | None = None,
        accept_value: ValuePredicate | None = None,
    ) -> int:
        response_timeout = self.timeout if timeout is None else timeout
        retry_count = self.retries if retries is None else retries
        attempts = retry_count + 1
        for attempt in range(1, attempts + 1):
            self.send(frame)
            try:
                return self._wait_for_response(
                    command, session, response_timeout, accept_value
                )
            except ResponseTimeout:
                if attempt == attempts:
                    raise ResponseTimeout(command, attempts) from None
                LOGGER.debug("retrying %s (%d/%d)", command.name, attempt, attempts)
        raise AssertionError("unreachable")

    def _request(
        self,
        command: Command,
        *,
        parameter: int = 0,
        value: int = 0,
        timeout: float | None = None,
        retries: int | None = None,
        query: bool = False,
        prepare: bool = True,
        accept_value: ValuePredicate | None = None,
    ) -> int:
        if prepare:
            self._prepare_target()
        else:
            self._ensure_source()
        session = self._next_query_session() if query else self.session
        frame = encode_control_request(
            self.source_address,
            self.address,
            command,
            session,
            parameter=parameter,
            value=value,
        )
        return self._exchange(
            frame,
            command,
            session,
            timeout=timeout,
            retries=retries,
            accept_value=accept_value,
        )

    @staticmethod
    def _require_ack_value(command: Command, actual: int, expected: int) -> int:
        if actual != expected:
            raise ProtocolError(
                f"{command.name} ACK returned {actual}; expected exactly {expected}"
            )
        return actual

    def _validate_identify(self, capabilities: int) -> int:
        try:
            return _validate_identify_capabilities(capabilities, self.address)
        except ProtocolError:
            self._profile_validated = False
            raise

    def identify(self) -> int:
        self._profile_validated = False
        capabilities = self._validate_identify(
            self._request(Command.IDENTIFY, query=True)
        )
        self._profile_validated = True
        return capabilities

    def _ensure_profile(self) -> None:
        if not self._profile_validated:
            self.identify()

    def info(self, selector: InfoSelector | int) -> int:
        try:
            selector_value = int(InfoSelector(selector))
        except ValueError as exc:
            raise ConfigurationError(f"unknown INFO selector {selector}") from exc
        self._ensure_profile()
        return self._request(
            Command.INFO,
            parameter=selector_value,
            query=True,
        )

    def read_device_info(self) -> DeviceInfo:
        self._prepare_target()
        assert self.target_name is not None
        capabilities = self._validate_identify(
            self.info(InfoSelector.CAPABILITIES)
        )
        app_base = self.info(InfoSelector.APP_BASE)
        app_max_size = self.info(InfoSelector.APP_MAX_SIZE)
        flash_erase_unit_size = self._read_flash_erase_unit_size()
        stored_size = self.info(InfoSelector.STORED_SIZE)
        stored_crc = self.info(InfoSelector.STORED_CRC)
        uid_words = (
            self.info(InfoSelector.UID_WORD_0),
            self.info(InfoSelector.UID_WORD_1),
            self.info(InfoSelector.UID_WORD_2),
        )
        reported_name = J1939Name(
            self.info(InfoSelector.NAME_LOW)
            | (self.info(InfoSelector.NAME_HIGH) << 32)
        )
        reported_address = self.info(InfoSelector.SOURCE_ADDRESS)
        if reported_name != self.target_name or reported_address != self.address:
            raise TargetIdentityError(
                "INFO identity does not match the locked Address Claimed identity"
            )
        return DeviceInfo(
            address=self.address,
            name=self.target_name,
            capabilities=capabilities,
            app_base=app_base,
            app_max_size=app_max_size,
            flash_erase_unit_size=flash_erase_unit_size,
            stored_size=stored_size,
            stored_crc=stored_crc,
            uid_words=uid_words,
        )

    def start_update(self, size: int) -> int:
        self._ensure_profile()
        actual = self._request(Command.START_UPDATE, value=size)
        if not 1 <= actual <= UPDATE_BLOCK_SIZE:
            raise ProtocolError(
                f"START_UPDATE ACK returned invalid block size {actual}; "
                f"expected 1..{UPDATE_BLOCK_SIZE}"
            )
        return actual

    def set_image_crc(self, crc: int) -> int:
        self._ensure_profile()
        actual = self._request(Command.SET_IMAGE_CRC, value=crc)
        return self._require_ack_value(Command.SET_IMAGE_CRC, actual, crc)

    def finalize(self, expected_crc: int) -> int:
        self._ensure_profile()
        actual = self._request(Command.FINALIZE, value=expected_crc)
        return self._require_ack_value(Command.FINALIZE, actual, expected_crc)

    def verify(self, expected_crc: int | None = None) -> int:
        self._ensure_profile()
        actual = self._request(Command.VERIFY)
        if expected_crc is not None and actual != expected_crc:
            raise VerificationError(
                f"VERIFY returned 0x{actual:08X}; expected 0x{expected_crc:08X}"
            )
        return actual

    def run_application(self, expected_crc: int | None = None) -> int:
        self._ensure_profile()
        actual = self._request(Command.RUN_APPLICATION)
        if expected_crc is not None:
            self._require_ack_value(Command.RUN_APPLICATION, actual, expected_crc)
        return actual

    def abort(self) -> int:
        self._ensure_profile()
        actual = self._request(Command.ABORT)
        return self._require_ack_value(Command.ABORT, actual, 0)

    def _abort_best_effort(self) -> None:
        try:
            self._ensure_source()
            frame = encode_control_request(
                self.source_address,
                self.address,
                Command.ABORT,
                self.session,
            )
            self.send(frame)
        except Exception:
            LOGGER.debug("best-effort ABORT could not be sent", exc_info=True)

    def _wait_dm15(self, timeout: float, *, terminal: bool = False) -> Dm15:
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise ResponseTimeout(PGN_DM15)
            frame = (
                self._deferred_dm15.popleft()
                if self._deferred_dm15
                else self.recv(remaining)
            )
            if frame is None:
                raise ResponseTimeout(PGN_DM15)
            try:
                identifier = identifier_from_frame(frame)
            except ProtocolError:
                continue
            self._observe_frame(frame)
            if identifier.pgn == PGN_ADDRESS_CLAIMED:
                continue
            if identifier.pgn != PGN_DM15:
                continue
            try:
                response = decode_dm15(frame)
            except ProtocolError:
                continue
            if (
                response.source_address != self.address
                or response.destination_address != self.source_address
            ):
                continue
            if response.status is Dm15Status.BUSY or (
                terminal and response.status is Dm15Status.PROCEED
            ):
                continue
            return response

    def _send_dm14(
        self,
        command: Dm14Command,
        *,
        length: int,
        pointer: int,
    ) -> None:
        self.send(
            encode_dm14(
                self.source_address,
                self.address,
                command,
                length=length,
                pointer=pointer,
                key=0xFFFF,
            )
        )

    def _close_dm14(self, *, length: int, pointer: int) -> None:
        # The close has no positive response. Reuse the configured retry budget
        # proactively; the target treats exact duplicates as idempotent/silent.
        for _ in range(self.retries + 1):
            self._send_dm14(
                Dm14Command.OPERATION_COMPLETED,
                length=length,
                pointer=pointer,
            )

    @staticmethod
    def _require_dm15_status(
        response: Dm15, expected: Dm15Status, operation: str
    ) -> None:
        if response.status is Dm15Status.OPERATION_FAILED:
            raise BlockTransferError(
                f"{operation} failed in DM15 (error 0x{response.error_code:08X})"
            )
        if response.status is not expected:
            raise ProtocolError(
                f"{operation} returned DM15 {response.status.name}; "
                f"expected {expected.name}"
            )

    @staticmethod
    def _require_dm15_length(response: Dm15, expected: int, operation: str) -> None:
        if response.allowed_length != expected:
            raise ProtocolError(
                f"{operation} returned DM15 length {response.allowed_length}; "
                f"expected exactly {expected}"
            )

    def _read_flash_erase_unit_size(self) -> int:
        try:
            unit_size = self.info(InfoSelector.FLASH_ERASE_UNIT_SIZE)
        except NackError as exc:
            if exc.code is not NackCode.INVALID_COMMAND:
                raise
            LOGGER.warning(
                "target does not advertise its Flash erase unit; "
                "using the legacy value %d bytes",
                FLASH_ERASE_BLOCK_SIZE,
            )
            return FLASH_ERASE_BLOCK_SIZE
        if unit_size <= 0 or (unit_size & (unit_size - 1)) != 0:
            raise ProtocolError(
                f"device reported invalid Flash erase unit size {unit_size}"
            )
        return unit_size

    def erase(
        self,
        app_base: int,
        image_size: int,
        *,
        erase_unit_size: int | None = None,
    ) -> int:
        if image_size <= 0:
            raise ConfigurationError("erase image size must be greater than zero")
        if erase_unit_size is None:
            erase_unit_size = self._read_flash_erase_unit_size()
        if erase_unit_size <= 0:
            raise ConfigurationError(
                "Flash erase unit size must be greater than zero"
            )
        self._ensure_profile()
        page_count = math.ceil(image_size / erase_unit_size)
        self._prepare_target()
        last_error: ResponseTimeout | None = None
        for attempt in range(1, self.retries + 2):
            self._send_dm14(
                Dm14Command.ERASE,
                length=page_count,
                pointer=app_base,
            )
            try:
                response = self._wait_dm15(self.erase_timeout)
                self._require_dm15_length(response, page_count, "erase")
                if response.status is Dm15Status.PROCEED:
                    response = self._wait_dm15(self.erase_timeout, terminal=True)
                    self._require_dm15_length(response, page_count, "erase")
                self._require_dm15_status(
                    response, Dm15Status.OPERATION_COMPLETED, "erase"
                )
                self._close_dm14(length=page_count, pointer=app_base)
                return page_count
            except ResponseTimeout as exc:
                last_error = exc
                if attempt > self.retries:
                    break
                LOGGER.debug(
                    "retrying DM14 erase (%d/%d)", attempt, self.retries + 1
                )
        raise BlockTransferError(
            f"erase was not confirmed after {self.retries + 1} attempts: "
            f"{last_error}"
        ) from last_error

    def _write_block_once(self, pointer: int, block: bytes) -> None:
        self._send_dm14(Dm14Command.WRITE, length=len(block), pointer=pointer)
        proceed = self._wait_dm15(self.timeout)
        self._require_dm15_length(proceed, len(block), "write")
        if proceed.status is Dm15Status.OPERATION_COMPLETED:
            self._close_dm14(length=len(block), pointer=pointer)
            return
        self._require_dm15_status(proceed, Dm15Status.PROCEED, "write")
        if proceed.seed not in (0, 0xFFFF):
            raise BlockTransferError(
                "device requested DM14 security, unsupported by this profile"
            )
        assert self.transport is not None
        transport_error: TransportError | None = None
        if len(block) <= 7:
            self.send(encode_dm16(self.source_address, self.address, block))
        else:
            try:
                self.transport.send_rts_cts(
                    PGN_DM16,
                    self.address,
                    encode_dm16_transport_payload(block),
                )
            except TransportError as exc:
                transport_error = exc

        completed = self._wait_dm15(self.timeout, terminal=True)
        self._require_dm15_length(completed, len(block), "write")
        if transport_error is not None:
            if completed.status is Dm15Status.OPERATION_COMPLETED:
                self._close_dm14(length=len(block), pointer=pointer)
                return
            if (
                completed.status is Dm15Status.OPERATION_FAILED
                and completed.error_code
                not in (int(NackCode.INVALID_SEQUENCE), int(NackCode.TIMEOUT))
            ):
                self._require_dm15_status(
                    completed, Dm15Status.OPERATION_COMPLETED, "write"
                )
            raise transport_error
        self._require_dm15_status(
            completed, Dm15Status.OPERATION_COMPLETED, "write"
        )
        self._close_dm14(length=len(block), pointer=pointer)

    def write_block(self, pointer: int, block: bytes | bytearray | memoryview) -> None:
        payload = bytes(block)
        if not 1 <= len(payload) <= UPDATE_BLOCK_SIZE:
            raise ConfigurationError(
                f"update block must contain 1..{UPDATE_BLOCK_SIZE} bytes"
            )
        self._ensure_profile()
        self._prepare_target()
        last_error: BaseException | None = None
        for attempt in range(1, self.retries + 2):
            try:
                self._write_block_once(pointer, payload)
                return
            except (ResponseTimeout, TransportError) as exc:
                last_error = exc
                if attempt > self.retries:
                    break
                LOGGER.debug(
                    "retrying DM14/DM16 block at 0x%08X (%d/%d)",
                    pointer,
                    attempt,
                    self.retries + 1,
                )
        raise BlockTransferError(
            f"block at 0x{pointer:08X} was not committed after "
            f"{self.retries + 1} attempts: {last_error}"
        ) from last_error

    def _wait_for_reclaim(self, timeout: float) -> None:
        assert self.target_name is not None
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise ResponseTimeout("Address Claimed after reset")
            iteration_started = time.monotonic()
            book = self.claim_manager.request_claims(min(self.scan_timeout, remaining))
            claim = book.by_name(self.target_name)
            if claim is None or claim.observed_at < iteration_started:
                continue
            if claim.address != self.address:
                raise TargetIdentityError(
                    f"target rebooted at 0x{claim.address:02X}, expected "
                    f"0x{self.address:02X}"
                )

            # A claim observed just after ENTER may still have been emitted by
            # the application before its delayed reset. Only accept the
            # transition once a directed IDENTIFY reports bootloader mode.
            self._prepared = True
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                self._prepared = False
                raise ResponseTimeout("bootloader IDENTIFY after reset")
            try:
                capabilities = self._validate_identify(
                    self._request(
                        Command.IDENTIFY,
                        query=True,
                        prepare=False,
                        timeout=min(self.timeout, remaining),
                        retries=0,
                    )
                )
            except (ResponseTimeout, NackError):
                self._prepared = False
                self._profile_validated = False
                continue
            if not capabilities & CAPABILITY_APPLICATION_RUNNING:
                self._profile_validated = True
                return
            self._prepared = False
            self._profile_validated = False

    def enter_bootloader(self, *, wait: bool = True) -> int:
        self._ensure_profile()
        actual = self._request(Command.ENTER_BOOTLOADER)
        self._require_ack_value(Command.ENTER_BOOTLOADER, actual, 0)
        if wait:
            self._prepared = False
            self._profile_validated = False
            self._wait_for_reclaim(self.erase_timeout)
        return actual

    def set_address(self, new_address: int) -> int:
        validate_unicast_address(new_address, "new address")
        self._ensure_profile()
        if new_address == self.source_address:
            raise ConfigurationError(
                "new target address cannot equal the service-tool source address"
            )
        # Commanded Address is global and has no application-level ACK. The
        # profile validation above must complete before broadcasting it.
        assert self.target_name is not None and self.transport is not None
        old_address = self.address
        payload = encode_commanded_address_payload(self.target_name, new_address)
        last_identity_error: TargetIdentityError | None = None
        for attempt in range(1, self.retries + 2):
            started = time.monotonic()
            stable_since: float | None = None
            self.transport.send_bam(PGN_COMMANDED_ADDRESS, payload)
            retry_not_before = time.monotonic() + self.transport_timeout
            deadline = max(
                time.monotonic() + self.erase_timeout,
                retry_not_before,
            )
            while True:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    break
                book = self.claim_manager.request_claims(
                    min(self.scan_timeout, remaining)
                )
                claim = book.by_name(self.target_name)
                if claim is None or claim.observed_at < started:
                    continue
                if claim.address != new_address:
                    last_identity_error = TargetIdentityError(
                        f"target {self.target_name} did not retain commanded "
                        f"address 0x{new_address:02X}; it claimed "
                        f"0x{claim.address:02X}"
                    )
                    # The preceding BAM may still be incomplete in the target.
                    # Do not overlap a retry with its TP timeout window.
                    if time.monotonic() >= retry_not_before:
                        break
                    continue
                if stable_since is None:
                    stable_since = claim.observed_at
                if (
                    time.monotonic() - stable_since
                    < self.claim_manager.claim_timeout
                ):
                    continue
                occupants = book.at_address(new_address)
                if len(occupants) != 1 or occupants[0].name != self.target_name:
                    continue

                # A claim starts, rather than completes, address arbitration.
                # Accept only after a quiet window, a unique NAME mapping and
                # a directed CanLoader IDENTIFY at the new route.
                self.address = new_address
                self._profile_validated = False
                try:
                    remaining = deadline - time.monotonic()
                    if remaining <= 0:
                        raise ResponseTimeout("Commanded Address IDENTIFY")
                    capabilities = self._request(
                        Command.IDENTIFY,
                        query=True,
                        prepare=False,
                        timeout=min(self.timeout, remaining),
                        retries=0,
                    )
                    self._validate_identify(capabilities)
                except ResponseTimeout:
                    self.address = old_address
                    self._prepared = False
                    self._profile_validated = False
                    continue
                except BaseException:
                    self.address = old_address
                    self._prepared = False
                    self._profile_validated = False
                    raise
                self._prepared = True
                self._profile_validated = True
                return new_address

            self.address = old_address
            self._prepared = False
            self._profile_validated = False
            if attempt <= self.retries:
                LOGGER.debug(
                    "retrying Commanded Address BAM (%d/%d)",
                    attempt,
                    self.retries + 1,
                )

        if last_identity_error is not None:
            raise last_identity_error
        raise ResponseTimeout("Commanded Address claim", self.retries + 1)

    def update(
        self,
        image: bytes | bytearray | memoryview,
        *,
        run: bool = False,
        progress: ProgressCallback | None = None,
    ) -> UpdateResult:
        firmware = bytes(image)
        if len(firmware) < MIN_IMAGE_SIZE:
            raise ConfigurationError(
                f"firmware must contain at least {MIN_IMAGE_SIZE} bytes "
                "(initial stack pointer and reset vector)"
            )

        capabilities = self.identify()
        if capabilities & CAPABILITY_APPLICATION_RUNNING:
            self.enter_bootloader(wait=True)
            capabilities = self.identify()
        if capabilities & CAPABILITY_APPLICATION_RUNNING:
            raise ProtocolError("target still reports application mode after reset")

        app_base = self.info(InfoSelector.APP_BASE)
        app_max_size = self.info(InfoSelector.APP_MAX_SIZE)
        flash_erase_unit_size = self._read_flash_erase_unit_size()
        if app_max_size < MIN_IMAGE_SIZE:
            raise ProtocolError(
                f"device reported invalid APP_MAX_SIZE value {app_max_size}"
            )
        if len(firmware) > app_max_size:
            raise ConfigurationError(
                f"firmware is {len(firmware)} bytes, but device APP_MAX_SIZE "
                f"is {app_max_size} bytes"
            )

        image_crc = crc32_iso_hdlc(firmware)
        verified = False
        try:
            block_size = self.start_update(len(firmware))
            self.set_image_crc(image_crc)
            self.erase(
                app_base,
                len(firmware),
                erase_unit_size=flash_erase_unit_size,
            )
            if progress is not None:
                progress(0, len(firmware))
            for offset in range(0, len(firmware), block_size):
                block = firmware[offset : offset + block_size]
                self.write_block(app_base + offset, block)
                if progress is not None:
                    progress(offset + len(block), len(firmware))
            self.finalize(image_crc)
            verified_crc = self.verify(image_crc)
            verified = True
            if run:
                self.run_application(image_crc)
            assert self.target_name is not None
            return UpdateResult(
                size=len(firmware),
                crc=image_crc,
                verified_crc=verified_crc,
                ran_application=run,
                target_name=self.target_name,
            )
        except BaseException:
            if not verified:
                self._abort_best_effort()
            raise
