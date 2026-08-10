"""High-level, retrying CanLoader client."""

from collections.abc import Callable
from dataclasses import dataclass
import logging
import math
import secrets
import time

from .bus import CanBus
from .constants import (
    MAX_BLOCK_SIZE,
    MAX_DATA_PAYLOAD,
    MIN_IMAGE_SIZE,
    PROTOCOL_VERSION,
    Command,
    InfoSelector,
    NackCode,
    ResponseKind,
)
from .crc import crc32_iso_hdlc
from .errors import (
    BlockTransferError,
    CanBusError,
    ConfigurationError,
    DeviceSessionTimeout,
    NackError,
    ProtocolError,
    ResponseTimeout,
    VerificationError,
)
from .protocol import (
    CanFrame,
    decode_response,
    encode_begin_block,
    encode_control_request,
    encode_data_frame,
    response_id,
    validate_node,
)


LOGGER = logging.getLogger(__name__)
ProgressCallback = Callable[[int, int], None]
ValuePredicate = Callable[[int], bool]


@dataclass(frozen=True, slots=True)
class DeviceInfo:
    capabilities: int
    app_base: int
    app_max_size: int
    stored_size: int
    stored_crc: int


@dataclass(frozen=True, slots=True)
class UpdateResult:
    size: int
    crc: int
    verified_crc: int
    ran_application: bool


class BootloaderClient:
    def __init__(
        self,
        bus: CanBus,
        *,
        node: int = 1,
        timeout: float = 0.5,
        erase_timeout: float = 4.0,
        retries: int = 3,
        session: int | None = None,
    ) -> None:
        validate_node(node)
        if not math.isfinite(timeout) or timeout <= 0:
            raise ConfigurationError("timeout must be greater than zero")
        if not math.isfinite(erase_timeout) or erase_timeout <= 0:
            raise ConfigurationError("erase timeout must be greater than zero")
        if retries < 0:
            raise ConfigurationError("retries cannot be negative")
        if session is None:
            session = secrets.randbelow(0xFF) + 1
        if not 1 <= session <= 0xFF:
            raise ConfigurationError("session must be between 1 and 255")

        self.bus = bus
        self.node = node
        self.timeout = timeout
        self.erase_timeout = erase_timeout
        self.retries = retries
        self.session = session

    def _send(self, frame: CanFrame) -> None:
        LOGGER.debug(
            "TX %03X [%d] %s",
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

    def _wait_for_response(
        self,
        command: Command,
        timeout: float,
        accept_value: ValuePredicate | None = None,
    ) -> int:
        deadline = time.monotonic() + timeout
        expected_id = response_id(self.node)
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise ResponseTimeout(command)
            try:
                frame = self.bus.recv(remaining)
            except CanBusError:
                raise
            except Exception as exc:
                raise CanBusError(f"failed to receive CAN frame: {exc}") from exc
            if frame is None:
                raise ResponseTimeout(command)

            LOGGER.debug(
                "RX %03X [%d] %s",
                frame.arbitration_id,
                frame.dlc,
                frame.data.hex(" "),
            )
            if frame.is_extended_id or frame.arbitration_id != expected_id:
                continue
            try:
                response = decode_response(frame)
            except ProtocolError:
                continue
            if (
                response.version != PROTOCOL_VERSION
                or response.session != self.session
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
                LOGGER.debug(
                    "ignoring stale %s ACK value %d",
                    command.name,
                    response.value,
                )
                continue
            return response.value

    def _exchange(
        self,
        frame: CanFrame,
        command: Command,
        *,
        timeout: float | None = None,
        retries: int | None = None,
        accept_value: ValuePredicate | None = None,
    ) -> int:
        response_timeout = self.timeout if timeout is None else timeout
        retry_count = self.retries if retries is None else retries
        attempts = retry_count + 1
        for attempt in range(1, attempts + 1):
            self._send(frame)
            try:
                return self._wait_for_response(
                    command, response_timeout, accept_value
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
        value: int = 0,
        timeout: float | None = None,
    ) -> int:
        frame = encode_control_request(
            self.node, command, self.session, value=value
        )
        return self._exchange(frame, command, timeout=timeout)

    @staticmethod
    def _require_ack_value(command: Command, actual: int, expected: int) -> int:
        if actual != expected:
            raise ProtocolError(
                f"{command.name} ACK returned {actual}; expected exactly {expected}"
            )
        return actual

    def discover(self) -> int:
        return self._request(Command.DISCOVER)

    def info(self, selector: InfoSelector | int) -> int:
        try:
            selector_value = int(InfoSelector(selector))
        except ValueError as exc:
            raise ConfigurationError(f"unknown INFO selector {selector}") from exc
        return self._request(Command.INFO, value=selector_value)

    def read_device_info(self) -> DeviceInfo:
        return DeviceInfo(
            capabilities=self.info(InfoSelector.CAPABILITIES),
            app_base=self.info(InfoSelector.APP_BASE),
            app_max_size=self.info(InfoSelector.APP_MAX_SIZE),
            stored_size=self.info(InfoSelector.STORED_SIZE),
            stored_crc=self.info(InfoSelector.STORED_CRC),
        )

    def start_update(self, size: int) -> int:
        actual = self._request(Command.START_UPDATE, value=size)
        return self._require_ack_value(
            Command.START_UPDATE, actual, MAX_BLOCK_SIZE
        )

    def set_image_crc(self, crc: int) -> int:
        actual = self._request(Command.SET_IMAGE_CRC, value=crc)
        return self._require_ack_value(Command.SET_IMAGE_CRC, actual, crc)

    def erase(self, expected_size: int | None = None) -> int:
        actual = self._request(Command.ERASE, timeout=self.erase_timeout)
        if expected_size is not None:
            self._require_ack_value(Command.ERASE, actual, expected_size)
        return actual

    def finish(self, expected_crc: int | None = None) -> int:
        actual = self._request(Command.FINISH)
        if expected_crc is not None:
            self._require_ack_value(Command.FINISH, actual, expected_crc)
        return actual

    def verify(self, expected_crc: int | None = None) -> int:
        actual_crc = self._request(Command.VERIFY)
        if expected_crc is not None and actual_crc != expected_crc:
            raise VerificationError(
                f"VERIFY returned 0x{actual_crc:08X}; expected 0x{expected_crc:08X}"
            )
        return actual_crc

    def run_application(self, expected_crc: int | None = None) -> int:
        actual = self._request(Command.RUN_APPLICATION)
        if expected_crc is not None:
            self._require_ack_value(Command.RUN_APPLICATION, actual, expected_crc)
        return actual

    def abort(self) -> int:
        actual = self._request(Command.ABORT)
        return self._require_ack_value(Command.ABORT, actual, 0)

    def _abort_best_effort(self) -> None:
        try:
            self._send(
                encode_control_request(self.node, Command.ABORT, self.session)
            )
        except Exception:
            LOGGER.debug("best-effort ABORT could not be sent", exc_info=True)

    @staticmethod
    def _is_retryable_block_nack(error: NackError) -> bool:
        return error.command in (Command.BEGIN_BLOCK, Command.DATA) and error.code in (
            NackCode.INVALID_SEQUENCE,
            NackCode.TIMEOUT,
        )

    def _transfer_block(
        self,
        block_index: int,
        block: bytes,
        total_size: int,
        progress: ProgressCallback | None,
    ) -> None:
        block_start = block_index * MAX_BLOCK_SIZE
        block_end = block_start + len(block)
        begin = encode_begin_block(
            self.node, self.session, block_index, len(block)
        )
        attempts = self.retries + 1
        last_error: Exception | None = None

        for attempt in range(1, attempts + 1):
            try:
                committed = self._exchange(
                    begin,
                    Command.BEGIN_BLOCK,
                    retries=0,
                    accept_value=lambda value: value in (block_start, block_end),
                )
                if committed == block_end:
                    if progress is not None:
                        progress(committed, total_size)
                    return
                if committed != block_start:
                    raise ProtocolError(
                        f"device reported partial block commit {committed}; "
                        f"expected exactly {block_start} or {block_end}"
                    )

                block_sequence = block_index & 0xFF
                for frame_sequence, offset in enumerate(
                    range(0, len(block), MAX_DATA_PAYLOAD)
                ):
                    self._send(
                        encode_data_frame(
                            self.node,
                            self.session,
                            block_sequence,
                            frame_sequence,
                            block[offset : offset + MAX_DATA_PAYLOAD],
                        )
                    )
                committed = self._wait_for_response(
                    Command.DATA,
                    self.timeout,
                    accept_value=lambda value: value == block_end,
                )
                if committed != block_end:
                    raise ProtocolError(
                        f"DATA ACK reported {committed} committed bytes; "
                        f"expected {block_end}"
                    )
                if progress is not None:
                    progress(committed, total_size)
                return
            except ResponseTimeout as exc:
                last_error = exc
            except NackError as exc:
                if not self._is_retryable_block_nack(exc):
                    raise
                last_error = exc

            if attempt < attempts:
                LOGGER.debug(
                    "retrying block %d (%d/%d)", block_index, attempt, attempts
                )

        detail = f": {last_error}" if last_error is not None else ""
        raise BlockTransferError(
            f"block {block_index} was not committed after {attempts} attempts"
            f"{detail}"
        ) from last_error

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

        self.discover()
        app_max_size = self.info(InfoSelector.APP_MAX_SIZE)
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
            self.start_update(len(firmware))
            self.set_image_crc(image_crc)
            self.erase(len(firmware))
            if progress is not None:
                progress(0, len(firmware))
            for block_index, offset in enumerate(
                range(0, len(firmware), MAX_BLOCK_SIZE)
            ):
                self._transfer_block(
                    block_index,
                    firmware[offset : offset + MAX_BLOCK_SIZE],
                    len(firmware),
                    progress,
                )
            self.finish(image_crc)
            verified_crc = self.verify(image_crc)
            verified = True
            if run:
                self.run_application(image_crc)
            return UpdateResult(
                size=len(firmware),
                crc=image_crc,
                verified_crc=verified_crc,
                ran_application=run,
            )
        except BaseException:
            if not verified:
                self._abort_best_effort()
            raise
