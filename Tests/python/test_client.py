from collections import deque
from collections.abc import Callable

import pytest

from canloader.client import BootloaderClient
from canloader.constants import (
    MAX_BLOCK_SIZE,
    Command,
    InfoSelector,
    NackCode,
    ResponseKind,
)
from canloader.crc import crc32_iso_hdlc
from canloader.errors import (
    BlockTransferError,
    ConfigurationError,
    DeviceSessionTimeout,
    NackError,
    ProtocolError,
    VerificationError,
)
from canloader.protocol import CanFrame, encode_response


Responder = Callable[[CanFrame, "FakeBus"], None]


class FakeBus:
    def __init__(self, responder: Responder) -> None:
        self.responder = responder
        self.sent: list[CanFrame] = []
        self.pending: deque[CanFrame] = deque()

    def send(self, frame: CanFrame) -> None:
        self.sent.append(frame)
        self.responder(frame, self)

    def recv(self, timeout: float) -> CanFrame | None:
        del timeout
        return self.pending.popleft() if self.pending else None

    def queue(self, frame: CanFrame) -> None:
        self.pending.append(frame)


def control_ack(frame: CanFrame, value: int = 0) -> CanFrame:
    return encode_response(
        1,
        ResponseKind.ACK,
        frame.data[1],
        frame.data[2],
        value,
    )


def test_request_retries_after_timeout() -> None:
    attempts = 0

    def responder(frame: CanFrame, bus: FakeBus) -> None:
        nonlocal attempts
        attempts += 1
        if attempts == 2:
            bus.queue(control_ack(frame, 0x55))

    bus = FakeBus(responder)
    client = BootloaderClient(bus, timeout=0.001, retries=1, session=7)

    assert client.discover() == 0x55
    assert attempts == 2


def test_matching_nack_raises_mapped_error() -> None:
    def responder(frame: CanFrame, bus: FakeBus) -> None:
        bus.queue(
            encode_response(
                1,
                ResponseKind.NACK,
                frame.data[1],
                frame.data[2],
                NackCode.INVALID_STATE,
            )
        )

    client = BootloaderClient(FakeBus(responder), session=8)
    with pytest.raises(NackError, match="current state") as caught:
        client.erase()
    assert caught.value.code is NackCode.INVALID_STATE


def test_unrelated_and_stale_responses_are_ignored() -> None:
    def responder(frame: CanFrame, bus: FakeBus) -> None:
        bus.queue(
            encode_response(
                2, ResponseKind.ACK, frame.data[1], frame.data[2], 1
            )
        )
        bus.queue(
            encode_response(
                1, ResponseKind.ACK, Command.INFO, frame.data[2], 2
            )
        )
        bus.queue(
            encode_response(
                1, ResponseKind.ACK, frame.data[1], frame.data[2] + 1, 3
            )
        )
        bus.queue(control_ack(frame, 4))

    client = BootloaderClient(FakeBus(responder), session=8)
    assert client.discover() == 4


def test_asynchronous_session_timeout_is_fatal_immediately() -> None:
    def responder(frame: CanFrame, bus: FakeBus) -> None:
        bus.queue(
            encode_response(
                1,
                ResponseKind.NACK,
                Command.ABORT,
                frame.data[2],
                NackCode.TIMEOUT,
            )
        )

    client = BootloaderClient(FakeBus(responder), retries=3, session=8)
    with pytest.raises(DeviceSessionTimeout, match="waiting for DISCOVER"):
        client.discover()


class UpdateDevice:
    def __init__(
        self,
        app_max_size: int = 4096,
        *,
        lose_first_data_ack: bool = False,
        corrupt_verify: bool = False,
    ) -> None:
        self.app_max_size = app_max_size
        self.lose_first_data_ack = lose_first_data_ack
        self.corrupt_verify = corrupt_verify
        self.expected_size = 0
        self.expected_crc = 0
        self.committed = 0
        self.current_block = bytearray()
        self.current_block_length = 0
        self.current_block_sequence = 0
        self.received_image = bytearray()
        self.commands: list[Command] = []
        self.received_data_frames = 0
        self.data_ack_lost = False

    def __call__(self, frame: CanFrame, bus: FakeBus) -> None:
        if frame.arbitration_id == 0x701:
            self._handle_data(frame, bus)
            return

        command = Command(frame.data[1])
        self.commands.append(command)
        if command is Command.INFO:
            selector = int.from_bytes(frame.data[4:8], "little")
            assert selector == InfoSelector.APP_MAX_SIZE
            bus.queue(control_ack(frame, self.app_max_size))
        elif command is Command.START_UPDATE:
            self.expected_size = int.from_bytes(frame.data[4:8], "little")
            bus.queue(control_ack(frame, MAX_BLOCK_SIZE))
        elif command is Command.SET_IMAGE_CRC:
            self.expected_crc = int.from_bytes(frame.data[4:8], "little")
            bus.queue(control_ack(frame, self.expected_crc))
        elif command is Command.BEGIN_BLOCK:
            block_index = int.from_bytes(frame.data[4:6], "little")
            block_start = block_index * MAX_BLOCK_SIZE
            assert frame.data[3] == (block_index & 0xFF)
            if self.committed < block_start:
                raise AssertionError("host skipped a block")
            self.current_block = bytearray()
            self.current_block_length = frame.data[6]
            self.current_block_sequence = frame.data[3]
            bus.queue(control_ack(frame, self.committed))
        elif command is Command.FINISH:
            assert self.committed == self.expected_size
            assert crc32_iso_hdlc(self.received_image) == self.expected_crc
            bus.queue(control_ack(frame, self.expected_crc))
        elif command is Command.VERIFY:
            verify_crc = (
                self.expected_crc ^ 1 if self.corrupt_verify else self.expected_crc
            )
            bus.queue(control_ack(frame, verify_crc))
        elif command is Command.ERASE:
            bus.queue(control_ack(frame, self.expected_size))
        elif command is Command.RUN_APPLICATION:
            bus.queue(control_ack(frame, self.expected_crc))
        elif command in (Command.DISCOVER, Command.ABORT):
            bus.queue(control_ack(frame))
        else:
            raise AssertionError(f"unexpected command {command}")

    def _handle_data(self, frame: CanFrame, bus: FakeBus) -> None:
        assert frame.data[0] != 0
        assert frame.data[1] == self.current_block_sequence
        expected_frame_sequence = len(self.current_block) // 5
        assert frame.data[2] == expected_frame_sequence
        self.current_block.extend(frame.data[3:])
        self.received_data_frames += 1
        if len(self.current_block) >= self.current_block_length:
            assert len(self.current_block) == self.current_block_length
            self.received_image.extend(self.current_block)
            self.committed += self.current_block_length
            if self.lose_first_data_ack and not self.data_ack_lost:
                self.data_ack_lost = True
                return
            bus.queue(
                encode_response(
                    1,
                    ResponseKind.ACK,
                    Command.DATA,
                    frame.data[0],
                    self.committed,
                )
            )


def test_lost_data_ack_uses_duplicate_begin_committed_count() -> None:
    device = UpdateDevice(lose_first_data_ack=True)
    bus = FakeBus(device)
    client = BootloaderClient(bus, timeout=0.001, retries=1, session=11)
    image = bytes(range(8))

    result = client.update(image)

    assert result.size == len(image)
    assert device.received_image == image
    assert device.received_data_frames == 2
    assert device.commands.count(Command.BEGIN_BLOCK) == 2


def test_begin_block_rejects_advanced_committed_count() -> None:
    def responder(frame: CanFrame, bus: FakeBus) -> None:
        bus.queue(control_ack(frame, 9))

    client = BootloaderClient(
        FakeBus(responder), timeout=0.001, retries=0, session=11
    )
    with pytest.raises(BlockTransferError, match="block 0"):
        client._transfer_block(0, b"12345678", 8, None)


def test_start_update_requires_exact_block_size_ack() -> None:
    def responder(frame: CanFrame, bus: FakeBus) -> None:
        bus.queue(control_ack(frame, MAX_BLOCK_SIZE - 1))

    client = BootloaderClient(FakeBus(responder), session=11)
    with pytest.raises(ProtocolError, match="expected exactly 100"):
        client.start_update(1024)


def test_full_update_workflow_and_progress() -> None:
    device = UpdateDevice()
    bus = FakeBus(device)
    client = BootloaderClient(bus, retries=1, session=12)
    image = bytes(range(113))
    progress: list[tuple[int, int]] = []

    result = client.update(
        image,
        run=True,
        progress=lambda done, total: progress.append((done, total)),
    )

    assert result.crc == crc32_iso_hdlc(image)
    assert result.verified_crc == result.crc
    assert result.ran_application is True
    assert device.expected_size == len(image)
    assert device.expected_crc == result.crc
    assert device.received_image == image
    assert device.commands == [
        Command.DISCOVER,
        Command.INFO,
        Command.START_UPDATE,
        Command.SET_IMAGE_CRC,
        Command.ERASE,
        Command.BEGIN_BLOCK,
        Command.BEGIN_BLOCK,
        Command.FINISH,
        Command.VERIFY,
        Command.RUN_APPLICATION,
    ]
    assert progress == [(0, 113), (100, 113), (113, 113)]


def test_update_validates_minimum_and_device_maximum_size() -> None:
    client = BootloaderClient(FakeBus(lambda frame, bus: None), session=1)
    with pytest.raises(ConfigurationError, match="at least 8 bytes"):
        client.update(b"short")

    device = UpdateDevice(app_max_size=8)
    client = BootloaderClient(FakeBus(device), session=2)
    with pytest.raises(ConfigurationError, match="APP_MAX_SIZE"):
        client.update(b"123456789")


def test_fatal_update_error_sends_best_effort_abort() -> None:
    device = UpdateDevice(corrupt_verify=True)
    client = BootloaderClient(FakeBus(device), session=3)

    with pytest.raises(VerificationError, match="VERIFY returned"):
        client.update(b"12345678")

    assert device.commands[-1] is Command.ABORT
