from collections import deque
from types import SimpleNamespace
import sys

from canloader.bus import PythonCanBus, open_bus
from canloader.j1939 import CanFrame


class FakeMessage:
    def __init__(
        self,
        *,
        arbitration_id: int,
        data: bytes,
        is_extended_id: bool,
        is_remote_frame: bool = False,
        is_error_frame: bool = False,
    ) -> None:
        self.arbitration_id = arbitration_id
        self.data = bytearray(data)
        self.is_extended_id = is_extended_id
        self.is_remote_frame = is_remote_frame
        self.is_error_frame = is_error_frame


class FakeCanModule:
    Message = FakeMessage


class FakePythonCanBus:
    def __init__(self) -> None:
        self.sent: list[tuple[FakeMessage, float]] = []
        self.incoming: deque[FakeMessage] = deque()
        self.closed = False

    def send(self, message: FakeMessage, timeout: float) -> None:
        self.sent.append((message, timeout))

    def recv(self, timeout: float) -> FakeMessage | None:
        del timeout
        return self.incoming.popleft() if self.incoming else None

    def shutdown(self) -> None:
        self.closed = True


def test_python_can_adapter_preserves_extended_frames() -> None:
    native = FakePythonCanBus()
    adapter = PythonCanBus(native, FakeCanModule, send_timeout=0.25)  # type: ignore[arg-type]

    adapter.send(CanFrame(0x18EF80F9, b"12345678"))
    sent, timeout = native.sent[0]
    assert sent.arbitration_id == 0x18EF80F9
    assert bytes(sent.data) == b"12345678"
    assert sent.is_extended_id is True
    assert timeout == 0.25

    native.incoming.append(
        FakeMessage(
            arbitration_id=0x18EFF980,
            data=b"abcdefgh",
            is_extended_id=True,
        )
    )
    assert adapter.recv(0.5) == CanFrame(0x18EFF980, b"abcdefgh")


def test_adapter_discards_11_bit_rtr_and_error_frames() -> None:
    native = FakePythonCanBus()
    adapter = PythonCanBus(native, FakeCanModule, send_timeout=0.25)  # type: ignore[arg-type]
    native.incoming.extend(
        (
            FakeMessage(arbitration_id=0x601, data=b"x", is_extended_id=False),
            FakeMessage(
                arbitration_id=0x18EF80F9,
                data=b"",
                is_extended_id=True,
                is_remote_frame=True,
            ),
            FakeMessage(
                arbitration_id=0,
                data=b"",
                is_extended_id=True,
                is_error_frame=True,
            ),
            FakeMessage(
                arbitration_id=0x18EF80F9,
                data=b"ok",
                is_extended_id=True,
            ),
        )
    )
    assert adapter.recv(0.5) == CanFrame(0x18EF80F9, b"ok")


def test_open_bus_passes_slcan_bitrate_and_serial_rate(monkeypatch) -> None:
    captured: dict[str, object] = {}
    native = FakePythonCanBus()

    def bus_factory(**kwargs):
        captured.update(kwargs)
        return native

    fake_can = SimpleNamespace(Bus=bus_factory, Message=FakeMessage)
    monkeypatch.setitem(sys.modules, "can", fake_can)
    adapter = open_bus(
        "slcan",
        "/dev/cu.usbmodem1",
        bitrate=500000,
        tty_baudrate=115200,
    )
    assert captured == {
        "interface": "slcan",
        "channel": "/dev/cu.usbmodem1@115200",
        "bitrate": 500000,
    }
    adapter.shutdown()
    assert native.closed is True


def test_open_bus_forces_extended_mode_for_seeedstudio(monkeypatch) -> None:
    captured: dict[str, object] = {}

    def bus_factory(**kwargs):
        captured.update(kwargs)
        return FakePythonCanBus()

    fake_can = SimpleNamespace(Bus=bus_factory, Message=FakeMessage)
    monkeypatch.setitem(sys.modules, "can", fake_can)
    open_bus(
        "seeedstudio",
        "/dev/cu.usbserial1",
        bitrate=500000,
        tty_baudrate=2000000,
    )
    assert captured == {
        "interface": "seeedstudio",
        "channel": "/dev/cu.usbserial1",
        "bitrate": 500000,
        "frame_type": "EXT",
        "baudrate": 2000000,
    }
