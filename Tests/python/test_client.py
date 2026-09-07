import math
import time

import pytest

from _fakes import FakeBus
from canloader.address_claim import encode_address_claim
from canloader.client import BootloaderClient, NetworkScanner
from canloader.constants import (
    CAPABILITY_APPLICATION_RUNNING,
    CAPABILITY_APPLICATION_VALID,
    REQUIRED_PROFILE_CAPABILITIES,
    TARGET_CODE,
    TARGET_CODE_STM32G0B1RE,
    FLASH_ERASE_BLOCK_SIZE,
    GLOBAL_ADDRESS,
    PGN_COMMANDED_ADDRESS,
    PGN_DM14,
    PGN_DM16,
    PGN_PROPRIETARY_A,
    PGN_REQUEST,
    PGN_TP_CM,
    PGN_TP_DT,
    PROTOCOL_VERSION,
    UPDATE_BLOCK_SIZE,
    Command,
    Dm14Command,
    Dm15Status,
    InfoSelector,
    NackCode,
    ResponseKind,
    TpAbortReason,
    TpControl,
)
from canloader.crc import crc32_iso_hdlc
from canloader.errors import (
    AddressClaimError,
    ConfigurationError,
    NackError,
    ProtocolError,
    ResponseTimeout,
    TargetIdentityError,
    VerificationError,
)
from canloader.j1939 import J1939Name, identifier_from_frame
from canloader.j1939 import make_frame
from canloader.protocol import (
    decode_commanded_address_payload,
    decode_control_request,
    decode_dm14,
    decode_dm16,
    encode_dm15,
    encode_response,
)
from canloader.transport import (
    decode_tp_cm,
    encode_tp_abort,
    encode_tp_cts,
    encode_tp_eom_ack,
)


class V2Device:
    def __init__(
        self,
        *,
        address: int = 0x80,
        name: int = 0x8000000000001234,
        app_max_size: int = 4096,
        application_running: bool = False,
        corrupt_verify: bool = False,
        target_code: int = TARGET_CODE,
        flash_erase_unit_size: int = FLASH_ERASE_BLOCK_SIZE,
        advertises_erase_unit: bool = True,
        profile_capabilities: int = REQUIRED_PROFILE_CAPABILITIES,
    ) -> None:
        self.address = address
        self.name = J1939Name(name)
        self.app_base = 0x08005000
        self.app_max_size = app_max_size
        self.application_running = application_running
        self.corrupt_verify = corrupt_verify
        self.target_code = target_code
        self.flash_erase_unit_size = flash_erase_unit_size
        self.advertises_erase_unit = advertises_erase_unit
        self.profile_capabilities = profile_capabilities
        self.expected_size = 0
        self.expected_crc = 0
        self.flash = bytearray()
        self.current_pointer = 0
        self.current_length = 0
        self.tp_pgn: int | None = None
        self.tp_size = 0
        self.tp_packets = 0
        self.tp_data = bytearray()
        self.tp_mode: TpControl | None = None
        self.commands: list[Command] = []
        self.dm14_commands: list[Dm14Command] = []
        self.writes = 0
        self.closed = 0
        self.completed_writes: set[tuple[int, int]] = set()
        self.drop_first_eom = False
        self._eom_dropped = False
        self.dm15_before_eom = False
        self.abort_first_tp = False
        self._tp_aborted = False
        self.drop_first_erase_completed = False
        self._erase_completed_dropped = False
        self.erase_completed = False
        self.delay_bootloader_transition = False
        self._reboot_pending = False
        self.drop_first_identify = False
        self._identify_dropped = False
        self.duplicate_write_proceed = False
        self.drop_first_commanded_address = False
        self.commanded_address_attempts = 0
        self.drop_first_commanded_address_dt2 = False
        self._commanded_address_dt2_dropped = False
        self.commanded_tp_timeout = 0.0
        self._commanded_tp_started = 0.0
        self.silence_after_commanded_address = False

    @property
    def capabilities(self) -> int:
        value = (self.target_code << 24) | (PROTOCOL_VERSION << 16)
        value |= self.address << 8
        value |= CAPABILITY_APPLICATION_VALID | self.profile_capabilities
        if self.application_running:
            value |= CAPABILITY_APPLICATION_RUNNING
        return value

    def _queue_response(
        self,
        bus: FakeBus,
        request,
        value: int = 0,
        *,
        kind: ResponseKind = ResponseKind.ACK,
    ) -> None:
        bus.queue(
            encode_response(
                self.address,
                request.source_address,
                kind,
                request.command,
                request.session,
                value,
            )
        )

    def _queue_dm15(
        self,
        bus: FakeBus,
        destination: int,
        status: Dm15Status,
        *,
        length: int = 0,
        error: int = 0,
    ) -> None:
        bus.queue(
            encode_dm15(
                self.address,
                destination,
                status,
                allowed_length=length,
                error_indicator=error,
            )
        )

    def _commit(self, raw: bytes, bus: FakeBus, destination: int) -> None:
        assert len(raw) == self.current_length
        offset = self.current_pointer - self.app_base
        end = offset + len(raw)
        if len(self.flash) < max(end, self.expected_size):
            self.flash.extend(b"\xFF" * (max(end, self.expected_size) - len(self.flash)))
        operation = (self.current_pointer, self.current_length)
        self.flash[offset:end] = raw
        if operation not in self.completed_writes:
            self.completed_writes.add(operation)
            self.writes += 1
        self._queue_dm15(
            bus,
            destination,
            Dm15Status.OPERATION_COMPLETED,
            length=len(raw),
        )

    def handle(self, frame, bus: FakeBus) -> None:
        identifier = identifier_from_frame(frame)
        if identifier.pgn == PGN_REQUEST:
            if (
                self.silence_after_commanded_address
                and self.commanded_address_attempts
            ):
                return
            bus.queue(encode_address_claim(self.address, self.name))
            return
        if identifier.pgn == PGN_PROPRIETARY_A:
            if identifier.destination_address != self.address:
                return
            request = decode_control_request(frame)
            command = Command(request.command)
            self.commands.append(command)
            if command is Command.IDENTIFY:
                if self.drop_first_identify and not self._identify_dropped:
                    self._identify_dropped = True
                    return
                self._queue_response(bus, request, self.capabilities)
                if self._reboot_pending:
                    self.application_running = False
                    self._reboot_pending = False
                    bus.queue(encode_address_claim(self.address, self.name))
            elif command is Command.INFO:
                if (
                    request.parameter == InfoSelector.FLASH_ERASE_UNIT_SIZE
                    and not self.advertises_erase_unit
                ):
                    self._queue_response(
                        bus,
                        request,
                        NackCode.INVALID_COMMAND,
                        kind=ResponseKind.NACK,
                    )
                    return
                values = {
                    InfoSelector.CAPABILITIES: self.capabilities,
                    InfoSelector.APP_BASE: self.app_base,
                    InfoSelector.APP_MAX_SIZE: self.app_max_size,
                    InfoSelector.STORED_SIZE: self.expected_size,
                    InfoSelector.STORED_CRC: self.expected_crc,
                    InfoSelector.UID_WORD_0: 0x11111111,
                    InfoSelector.UID_WORD_1: 0x22222222,
                    InfoSelector.UID_WORD_2: 0x33333333,
                    InfoSelector.NAME_LOW: self.name.value & 0xFFFFFFFF,
                    InfoSelector.NAME_HIGH: self.name.value >> 32,
                    InfoSelector.SOURCE_ADDRESS: self.address,
                    InfoSelector.FLASH_ERASE_UNIT_SIZE: self.flash_erase_unit_size,
                }
                self._queue_response(bus, request, values[InfoSelector(request.parameter)])
            elif command is Command.START_UPDATE:
                self.expected_size = request.value
                self.flash = bytearray(b"\xFF" * self.expected_size)
                self._queue_response(bus, request, UPDATE_BLOCK_SIZE)
            elif command is Command.SET_IMAGE_CRC:
                self.expected_crc = request.value
                self._queue_response(bus, request, request.value)
            elif command is Command.FINALIZE:
                assert request.value == self.expected_crc
                assert crc32_iso_hdlc(self.flash[: self.expected_size]) == self.expected_crc
                self._queue_response(bus, request, request.value)
            elif command is Command.VERIFY:
                value = self.expected_crc ^ int(self.corrupt_verify)
                self._queue_response(bus, request, value)
            elif command is Command.RUN_APPLICATION:
                self._queue_response(bus, request, self.expected_crc)
            elif command is Command.ENTER_BOOTLOADER:
                self._queue_response(bus, request, 0)
                if self.delay_bootloader_transition:
                    self._reboot_pending = True
                else:
                    self.application_running = False
                bus.queue(encode_address_claim(self.address, self.name))
            elif command is Command.ABORT:
                self._queue_response(bus, request, 0)
            return
        if identifier.pgn == PGN_DM14:
            if identifier.destination_address != self.address:
                return
            request = decode_dm14(frame)
            self.dm14_commands.append(request.command)
            if request.command is Dm14Command.ERASE:
                assert request.pointer == self.app_base
                assert request.length == math.ceil(
                    self.expected_size / self.flash_erase_unit_size
                )
                if self.erase_completed:
                    self._queue_dm15(
                        bus,
                        request.source_address,
                        Dm15Status.OPERATION_COMPLETED,
                        length=request.length,
                    )
                else:
                    self._queue_dm15(
                        bus,
                        request.source_address,
                        Dm15Status.PROCEED,
                        length=request.length,
                    )
                    self.erase_completed = True
                    if (
                        not self.drop_first_erase_completed
                        or self._erase_completed_dropped
                    ):
                        self._queue_dm15(
                            bus,
                            request.source_address,
                            Dm15Status.OPERATION_COMPLETED,
                            length=request.length,
                        )
                    else:
                        self._erase_completed_dropped = True
            elif request.command is Dm14Command.WRITE:
                self.current_pointer = request.pointer
                self.current_length = request.length
                if (request.pointer, request.length) in self.completed_writes:
                    self._queue_dm15(
                        bus,
                        request.source_address,
                        Dm15Status.OPERATION_COMPLETED,
                        length=request.length,
                    )
                else:
                    self._queue_dm15(
                        bus,
                        request.source_address,
                        Dm15Status.PROCEED,
                        length=request.length,
                    )
                    if self.duplicate_write_proceed:
                        self._queue_dm15(
                            bus,
                            request.source_address,
                            Dm15Status.PROCEED,
                            length=request.length,
                        )
            elif request.command is Dm14Command.OPERATION_COMPLETED:
                self.closed += 1
            return
        if identifier.pgn == PGN_DM16:
            if identifier.destination_address != self.address:
                return
            self._commit(decode_dm16(frame).data, bus, identifier.source_address)
            return
        if identifier.pgn == PGN_TP_CM:
            _, cm = decode_tp_cm(frame)
            if cm.control is TpControl.RTS and identifier.destination_address == self.address:
                if self.abort_first_tp and not self._tp_aborted:
                    self._tp_aborted = True
                    bus.queue(
                        encode_tp_abort(
                            self.address,
                            identifier.source_address,
                            cm.pgn,
                            TpAbortReason.TIMEOUT,
                        )
                    )
                    self._queue_dm15(
                        bus,
                        identifier.source_address,
                        Dm15Status.OPERATION_FAILED,
                        length=self.current_length,
                        error=NackCode.TIMEOUT,
                    )
                    return
                self.tp_mode = TpControl.RTS
                self.tp_pgn = cm.pgn
                self.tp_size = cm.total_size or 0
                self.tp_packets = cm.packet_count or 0
                self.tp_data = bytearray()
                bus.queue(
                    encode_tp_cts(
                        self.address,
                        identifier.source_address,
                        cm.pgn,
                        self.tp_packets,
                        1,
                    )
                )
            elif cm.control is TpControl.BAM:
                if self.tp_mode is TpControl.BAM:
                    if (
                        self.commanded_tp_timeout <= 0
                        or time.monotonic() - self._commanded_tp_started
                        < self.commanded_tp_timeout
                    ):
                        return
                    self.tp_mode = None
                self.tp_mode = TpControl.BAM
                self.tp_pgn = cm.pgn
                self.tp_size = cm.total_size or 0
                self.tp_packets = cm.packet_count or 0
                self.tp_data = bytearray()
                self._commanded_tp_started = time.monotonic()
            return
        if identifier.pgn == PGN_TP_DT and self.tp_mode is not None:
            if (
                self.tp_mode is TpControl.BAM
                and self.tp_pgn == PGN_COMMANDED_ADDRESS
                and frame.data[0] == 2
                and self.drop_first_commanded_address_dt2
                and not self._commanded_address_dt2_dropped
            ):
                self._commanded_address_dt2_dropped = True
                return
            if (
                self.tp_mode is TpControl.BAM
                and self.tp_pgn == PGN_COMMANDED_ADDRESS
            ):
                self._commanded_tp_started = time.monotonic()
            self.tp_data.extend(frame.data[1:8])
            if frame.data[0] != self.tp_packets:
                return
            payload = bytes(self.tp_data[: self.tp_size])
            mode = self.tp_mode
            pgn = self.tp_pgn
            self.tp_mode = None
            if mode is TpControl.RTS:
                assert pgn == PGN_DM16
                assert payload[0] == 0xFF
                emit_eom = not self.drop_first_eom or self._eom_dropped
                if self.dm15_before_eom:
                    self._commit(payload[1:], bus, identifier.source_address)
                if emit_eom:
                    bus.queue(
                        encode_tp_eom_ack(
                            self.address,
                            identifier.source_address,
                            PGN_DM16,
                            len(payload),
                            self.tp_packets,
                        )
                    )
                else:
                    self._eom_dropped = True
                if not self.dm15_before_eom:
                    self._commit(payload[1:], bus, identifier.source_address)
            else:
                assert pgn == PGN_COMMANDED_ADDRESS
                name, new_address = decode_commanded_address_payload(payload)
                if name == self.name:
                    self.commanded_address_attempts += 1
                    if (
                        self.drop_first_commanded_address
                        and self.commanded_address_attempts == 1
                    ):
                        return
                    self.address = new_address
                    bus.queue(encode_address_claim(self.address, self.name))


def device_bus(*devices: V2Device) -> FakeBus:
    def responder(frame, bus: FakeBus) -> None:
        for device in devices:
            device.handle(frame, bus)

    return FakeBus(responder)


def make_client(bus: FakeBus, device: V2Device, **kwargs) -> BootloaderClient:
    options = {
        "address": device.address,
        "timeout": 0.001,
        "erase_timeout": 0.01,
        "claim_timeout": 0.001,
        "scan_timeout": 0.001,
        "retries": 1,
        "session": 7,
    }
    options.update(kwargs)
    return BootloaderClient(bus, **options)


def test_scanner_lists_generic_j1939_and_identifies_canloader() -> None:
    canloader = V2Device(address=0x80, name=0x8000000000000020)
    generic_name = J1939Name(0x8000000000000030)

    def responder(frame, bus: FakeBus) -> None:
        identifier = identifier_from_frame(frame)
        if identifier.pgn == PGN_REQUEST:
            bus.queue(encode_address_claim(canloader.address, canloader.name))
            bus.queue(encode_address_claim(0x81, generic_name))
        else:
            canloader.handle(frame, bus)

    devices = NetworkScanner(
        FakeBus(responder),
        timeout=0.001,
        identify_timeout=0.001,
        claim_timeout=0.001,
        session=9,
    ).scan()
    assert [(item.address, item.name) for item in devices] == [
        (0x80, canloader.name),
        (0x81, generic_name),
    ]
    assert devices[0].is_canloader is True
    assert devices[0].protocol_version == 2
    assert devices[1].is_canloader is False


@pytest.mark.parametrize(
    ("target_code", "profile_capabilities"),
    [
        (0xA5, REQUIRED_PROFILE_CAPABILITIES),
        (TARGET_CODE, REQUIRED_PROFILE_CAPABILITIES & ~0x20),
    ],
)
def test_identify_rejects_wrong_product_or_incomplete_profile(
    target_code: int, profile_capabilities: int
) -> None:
    device = V2Device(
        target_code=target_code,
        profile_capabilities=profile_capabilities,
    )
    client = make_client(device_bus(device), device, target_name=device.name)

    with pytest.raises(ProtocolError, match="IDENTIFY"):
        client.identify()


def test_identify_accepts_stm32g0b1re_target() -> None:
    device = V2Device(target_code=TARGET_CODE_STM32G0B1RE)
    client = make_client(device_bus(device), device, target_name=device.name)

    assert client.identify() == device.capabilities


@pytest.mark.parametrize(
    ("method", "args"),
    [
        ("info", (InfoSelector.CAPABILITIES,)),
        ("read_device_info", ()),
        ("start_update", (8,)),
        ("verify", ()),
        ("run_application", ()),
        ("abort", ()),
        ("enter_bootloader", ()),
        ("set_address", (0x92,)),
    ],
)
def test_public_operations_validate_the_canloader_profile_first(
    method: str, args: tuple[object, ...]
) -> None:
    device = V2Device(target_code=0xA5)
    client = make_client(device_bus(device), device, target_name=device.name)

    with pytest.raises(ProtocolError, match="target code"):
        getattr(client, method)(*args)

    assert device.commands == [Command.IDENTIFY]


def test_failed_explicit_revalidation_invalidates_the_profile_cache() -> None:
    device = V2Device()
    client = make_client(device_bus(device), device, target_name=device.name)
    client.identify()
    device.target_code = 0xA5

    with pytest.raises(ProtocolError, match="target code"):
        client.identify()
    with pytest.raises(ProtocolError, match="target code"):
        client.run_application()

    assert device.commands == [
        Command.IDENTIFY,
        Command.IDENTIFY,
        Command.IDENTIFY,
    ]


def test_lost_tool_address_invalidates_profile_and_reclaims_before_command() -> None:
    device = V2Device()
    bus = device_bus(device)
    client = make_client(bus, device, target_name=device.name)
    client.identify()
    claims_before = sum(
        identifier_from_frame(frame).pgn == 0xEE00
        and identifier_from_frame(frame).source_address == 0xF9
        for frame in bus.sent
    )

    with pytest.raises(AddressClaimError, match="won by lower NAME"):
        client._observe_frame(
            encode_address_claim(client.source_address, 0x8000000000000001)
        )

    client.run_application()
    claims_after = sum(
        identifier_from_frame(frame).pgn == 0xEE00
        and identifier_from_frame(frame).source_address == client.source_address
        for frame in bus.sent
    )
    assert client.source_address != 0xF9
    assert client.transport is not None
    assert client.transport.source_address == client.source_address
    assert claims_before >= 1
    assert claims_after >= 1
    assert device.commands == [
        Command.IDENTIFY,
        Command.IDENTIFY,
        Command.RUN_APPLICATION,
    ]


def test_scanner_does_not_label_wrong_target_code_as_canloader() -> None:
    device = V2Device(target_code=0xA5)
    devices = NetworkScanner(
        device_bus(device),
        timeout=0.001,
        identify_timeout=0.001,
        claim_timeout=0.001,
        session=9,
    ).scan()

    assert len(devices) == 1
    assert devices[0].name == device.name
    assert devices[0].is_canloader is False


def test_target_name_is_locked_before_control_operation() -> None:
    device = V2Device()
    client = make_client(
        device_bus(device),
        device,
        target_name=0x8000000000009999,
    )
    with pytest.raises(TargetIdentityError, match="not expected NAME"):
        client.identify()


def test_control_request_retries_without_reverting_to_v1_framing() -> None:
    device = V2Device()
    device.drop_first_identify = True
    bus = device_bus(device)
    client = make_client(bus, device)
    assert client.identify() == device.capabilities
    identify_frames = [
        frame
        for frame in bus.sent
        if identifier_from_frame(frame).pgn == PGN_PROPRIETARY_A
    ]
    assert len(identify_frames) == 2
    assert all(frame.is_extended_id for frame in bus.sent)


def test_client_answers_address_claim_request_while_waiting_for_control_ack() -> None:
    device = V2Device()

    def responder(frame, bus: FakeBus) -> None:
        identifier = identifier_from_frame(frame)
        if identifier.pgn == PGN_REQUEST:
            bus.queue(encode_address_claim(device.address, device.name))
        elif identifier.pgn == PGN_PROPRIETARY_A:
            request = decode_control_request(frame)
            bus.queue(
                make_frame(
                    PGN_REQUEST,
                    device.address,
                    (0xEE00).to_bytes(3, "little") + b"\xFF" * 5,
                    destination_address=GLOBAL_ADDRESS,
                )
            )
            bus.queue(
                encode_response(
                    device.address,
                    request.source_address,
                    ResponseKind.ACK,
                    request.command,
                    request.session,
                    device.capabilities,
                )
            )

    bus = FakeBus(responder)
    client = make_client(bus, device, target_name=device.name)
    assert client.identify() == device.capabilities
    host_claims = [
        frame
        for frame in bus.sent
        if identifier_from_frame(frame).pgn == 0xEE00
        and identifier_from_frame(frame).source_address == 0xF9
    ]
    assert len(host_claims) == 2


def test_full_update_uses_dm14_dm16_rts_cts_and_only_selected_device() -> None:
    target = V2Device(address=0x80, name=0x8000000000000100)
    other = V2Device(address=0x81, name=0x8000000000000200)
    bus = device_bus(target, other)
    client = make_client(bus, target, target_name=target.name)
    image = bytes(range(113))
    progress: list[tuple[int, int]] = []

    result = client.update(
        image,
        run=True,
        progress=lambda done, total: progress.append((done, total)),
    )

    assert result.crc == crc32_iso_hdlc(image)
    assert result.verified_crc == result.crc
    assert result.target_name == target.name
    assert target.flash[: len(image)] == image
    assert target.writes == 2
    assert other.writes == 0
    assert target.dm14_commands == [
        Dm14Command.ERASE,
        Dm14Command.OPERATION_COMPLETED,
        Dm14Command.OPERATION_COMPLETED,
        Dm14Command.WRITE,
        Dm14Command.OPERATION_COMPLETED,
        Dm14Command.OPERATION_COMPLETED,
        Dm14Command.WRITE,
        Dm14Command.OPERATION_COMPLETED,
        Dm14Command.OPERATION_COMPLETED,
    ]
    assert progress == [(0, 113), (100, 113), (113, 113)]
    tp_controls = [
        decode_tp_cm(frame)[1].control
        for frame in bus.sent
        if identifier_from_frame(frame).pgn == PGN_TP_CM
    ]
    assert tp_controls == [TpControl.RTS, TpControl.RTS]


def test_update_uses_erase_unit_advertised_by_stm32g0b1re() -> None:
    device = V2Device(
        target_code=TARGET_CODE_STM32G0B1RE,
        flash_erase_unit_size=2048,
        app_max_size=8192,
    )
    client = make_client(device_bus(device), device, target_name=device.name)

    result = client.update(bytes(index & 0xFF for index in range(2050)))

    assert result.size == 2050
    assert device.dm14_commands[0] is Dm14Command.ERASE
    assert device.flash[:2050] == bytes(index & 0xFF for index in range(2050))


def test_update_falls_back_for_pre_selector_11_firmware() -> None:
    device = V2Device(advertises_erase_unit=False)
    client = make_client(device_bus(device), device, target_name=device.name)

    result = client.update(bytes(range(113)))

    assert result.size == 113


def test_short_final_block_uses_single_frame_dm16() -> None:
    device = V2Device()
    bus = device_bus(device)
    client = make_client(bus, device, target_name=device.name)
    client.write_block(device.app_base, b"1234567")
    dm16_frames = [
        frame for frame in bus.sent if identifier_from_frame(frame).pgn == PGN_DM16
    ]
    assert len(dm16_frames) == 1
    assert dm16_frames[0].data == b"\x071234567"


def test_lost_eom_ack_uses_preserved_dm15_without_rewriting() -> None:
    device = V2Device()
    device.drop_first_eom = True
    bus = device_bus(device)
    client = make_client(bus, device, target_name=device.name)
    client.write_block(device.app_base, bytes(range(20)))
    assert device.writes == 1
    assert device.dm14_commands == [
        Dm14Command.WRITE,
        Dm14Command.OPERATION_COMPLETED,
        Dm14Command.OPERATION_COMPLETED,
    ]


def test_dm15_arriving_before_eom_ack_is_preserved() -> None:
    device = V2Device()
    device.dm15_before_eom = True
    bus = device_bus(device)
    client = make_client(bus, device, target_name=device.name, retries=0)
    client.write_block(device.app_base, bytes(range(20)))
    assert device.writes == 1
    assert device.dm14_commands == [
        Dm14Command.WRITE,
        Dm14Command.OPERATION_COMPLETED,
    ]


def test_tp_abort_consumes_terminal_dm15_before_retry() -> None:
    device = V2Device()
    device.abort_first_tp = True
    bus = device_bus(device)
    client = make_client(bus, device, target_name=device.name)
    client.write_block(device.app_base, bytes(range(20)))
    assert device.writes == 1
    assert device.dm14_commands == [
        Dm14Command.WRITE,
        Dm14Command.WRITE,
        Dm14Command.OPERATION_COMPLETED,
        Dm14Command.OPERATION_COMPLETED,
    ]


def test_duplicate_dm15_proceed_is_ignored_while_waiting_for_completion() -> None:
    device = V2Device()
    device.duplicate_write_proceed = True
    client = make_client(device_bus(device), device, target_name=device.name)

    client.write_block(device.app_base, bytes(range(20)))

    assert device.writes == 1
    assert device.dm14_commands == [
        Dm14Command.WRITE,
        Dm14Command.OPERATION_COMPLETED,
        Dm14Command.OPERATION_COMPLETED,
    ]


def test_lost_erase_completion_retries_dm14_idempotently() -> None:
    device = V2Device()
    device.expected_size = 100
    device.drop_first_erase_completed = True
    client = make_client(device_bus(device), device, target_name=device.name)
    assert client.erase(device.app_base, device.expected_size) == 1
    assert device.dm14_commands == [
        Dm14Command.ERASE,
        Dm14Command.ERASE,
        Dm14Command.OPERATION_COMPLETED,
        Dm14Command.OPERATION_COMPLETED,
    ]


def test_application_is_entered_into_bootloader_before_update() -> None:
    device = V2Device(application_running=True)
    client = make_client(device_bus(device), device, target_name=device.name)
    client.update(b"12345678")
    assert Command.ENTER_BOOTLOADER in device.commands
    assert device.application_running is False


def test_enter_wait_rejects_application_claim_until_bootloader_identifies() -> None:
    device = V2Device(application_running=True)
    device.delay_bootloader_transition = True
    client = make_client(device_bus(device), device, target_name=device.name)
    client.update(b"12345678")
    assert device.application_running is False
    # Initial app IDENTIFY, post-ACK app IDENTIFY, bootloader IDENTIFY, then the
    # normal update-side IDENTIFY.
    assert device.commands.count(Command.IDENTIFY) >= 4


def test_set_address_uses_commanded_address_bam_and_confirms_new_claim() -> None:
    device = V2Device()
    bus = device_bus(device)
    client = make_client(bus, device, target_name=device.name)
    assert client.set_address(0x92) == 0x92
    assert device.address == 0x92
    assert client.address == 0x92
    tp_controls = [
        decode_tp_cm(frame)[1]
        for frame in bus.sent
        if identifier_from_frame(frame).pgn == PGN_TP_CM
    ]
    assert len(tp_controls) == 1
    assert tp_controls[0].control is TpControl.BAM
    assert tp_controls[0].pgn == PGN_COMMANDED_ADDRESS


def test_set_address_retries_a_lost_commanded_address_bam() -> None:
    device = V2Device()
    device.drop_first_commanded_address = True
    bus = device_bus(device)
    client = make_client(
        bus,
        device,
        target_name=device.name,
        transport_timeout=0.001,
    )

    assert client.set_address(0x92) == 0x92
    assert device.commanded_address_attempts == 2
    assert sum(
        decode_tp_cm(frame)[1].control is TpControl.BAM
        for frame in bus.sent
        if identifier_from_frame(frame).pgn == PGN_TP_CM
    ) == 2


def test_set_address_waits_for_partial_bam_to_expire_before_retry() -> None:
    device = V2Device()
    device.drop_first_commanded_address_dt2 = True
    device.commanded_tp_timeout = 0.12
    bus = device_bus(device)
    client = make_client(
        bus,
        device,
        target_name=device.name,
        erase_timeout=0.01,
        transport_timeout=0.15,
    )

    assert client.set_address(0x92) == 0x92
    assert device._commanded_address_dt2_dropped is True
    assert sum(
        decode_tp_cm(frame)[1].control is TpControl.BAM
        for frame in bus.sent
        if identifier_from_frame(frame).pgn == PGN_TP_CM
    ) == 2


def test_set_address_timeout_reports_the_configured_attempt_count() -> None:
    device = V2Device()
    device.drop_first_commanded_address = True
    device.silence_after_commanded_address = True
    client = make_client(
        device_bus(device),
        device,
        target_name=device.name,
        retries=0,
        erase_timeout=0.001,
        transport_timeout=0.001,
    )

    with pytest.raises(ResponseTimeout) as captured:
        client.set_address(0x92)

    assert captured.value.attempts == 1
    assert str(captured.value).endswith("after 1 attempt")


def test_set_address_rejects_a_contested_new_address() -> None:
    target = V2Device(address=0x80, name=0x8000000000000100)
    lower_name_occupant = V2Device(address=0x92, name=0x8000000000000001)
    client = make_client(
        device_bus(target, lower_name_occupant),
        target,
        target_name=target.name,
        transport_timeout=0.002,
    )

    with pytest.raises(ResponseTimeout, match="Commanded Address claim"):
        client.set_address(0x92)

    assert client.address == 0x80


def test_update_size_validation_and_crc_failure_abort() -> None:
    device = V2Device(app_max_size=8)
    client = make_client(device_bus(device), device, target_name=device.name)
    with pytest.raises(ConfigurationError, match="at least 8"):
        client.update(b"short")
    with pytest.raises(ConfigurationError, match="APP_MAX_SIZE"):
        client.update(b"123456789")

    corrupt = V2Device(corrupt_verify=True)
    client = make_client(device_bus(corrupt), corrupt, target_name=corrupt.name)
    with pytest.raises(VerificationError, match="VERIFY returned"):
        client.update(b"12345678")
    assert corrupt.commands[-1] is Command.ABORT


def test_matching_control_nack_is_mapped() -> None:
    device = V2Device()

    def responder(frame, bus: FakeBus) -> None:
        identifier = identifier_from_frame(frame)
        if identifier.pgn == PGN_REQUEST:
            bus.queue(encode_address_claim(device.address, device.name))
        elif identifier.pgn == PGN_PROPRIETARY_A:
            request = decode_control_request(frame)
            bus.queue(
                encode_response(
                    device.address,
                    request.source_address,
                    ResponseKind.NACK,
                    request.command,
                    request.session,
                    NackCode.INVALID_STATE,
                )
            )

    client = make_client(FakeBus(responder), device, target_name=device.name)
    with pytest.raises(NackError, match="current state"):
        client.identify()
