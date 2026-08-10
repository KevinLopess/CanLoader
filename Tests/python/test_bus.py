from canloader.bus import PythonCanBus
from canloader.protocol import CanFrame


class FakeMessage:
    def __init__(
        self, *, arbitration_id: int, data: bytes, is_extended_id: bool
    ) -> None:
        self.arbitration_id = arbitration_id
        self.data = bytearray(data)
        self.is_extended_id = is_extended_id


class FakeCanModule:
    Message = FakeMessage


class FakePythonCanBus:
    def __init__(self) -> None:
        self.sent: list[tuple[FakeMessage, float]] = []
        self.incoming: FakeMessage | None = None
        self.closed = False

    def send(self, message: FakeMessage, timeout: float) -> None:
        self.sent.append((message, timeout))

    def recv(self, timeout: float) -> FakeMessage | None:
        assert timeout == 0.5
        return self.incoming

    def shutdown(self) -> None:
        self.closed = True


def test_python_can_adapter_converts_frames_without_importing_python_can() -> None:
    native = FakePythonCanBus()
    adapter = PythonCanBus(native, FakeCanModule, send_timeout=0.25)  # type: ignore[arg-type]

    adapter.send(CanFrame(0x601, b"12345678"))
    sent, timeout = native.sent[0]
    assert sent.arbitration_id == 0x601
    assert bytes(sent.data) == b"12345678"
    assert sent.is_extended_id is False
    assert timeout == 0.25

    native.incoming = FakeMessage(
        arbitration_id=0x581, data=b"abcdefgh", is_extended_id=False
    )
    received = adapter.recv(0.5)
    assert received == CanFrame(0x581, b"abcdefgh")

    adapter.shutdown()
    assert native.closed is True
