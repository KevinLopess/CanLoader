"""Adapter between the protocol's small bus API and python-can."""

from types import ModuleType
from typing import Any, Protocol

from .errors import CanBusError
from .protocol import CanFrame


class CanBus(Protocol):
    def send(self, frame: CanFrame) -> None: ...

    def recv(self, timeout: float) -> CanFrame | None: ...


class PythonCanBus:
    def __init__(
        self, native_bus: Any, can_module: ModuleType, send_timeout: float
    ) -> None:
        self._native_bus = native_bus
        self._can = can_module
        self._send_timeout = send_timeout

    def send(self, frame: CanFrame) -> None:
        message = self._can.Message(
            arbitration_id=frame.arbitration_id,
            data=frame.data,
            is_extended_id=frame.is_extended_id,
        )
        try:
            self._native_bus.send(message, timeout=self._send_timeout)
        except Exception as exc:
            raise CanBusError(f"failed to send CAN frame: {exc}") from exc

    def recv(self, timeout: float) -> CanFrame | None:
        try:
            message = self._native_bus.recv(timeout)
        except Exception as exc:
            raise CanBusError(f"failed to receive CAN frame: {exc}") from exc
        if message is None:
            return None
        return CanFrame(
            arbitration_id=int(message.arbitration_id),
            data=bytes(message.data),
            is_extended_id=bool(message.is_extended_id),
        )

    def shutdown(self) -> None:
        try:
            self._native_bus.shutdown()
        except Exception as exc:
            raise CanBusError(f"failed to close CAN interface: {exc}") from exc

    def __enter__(self) -> "PythonCanBus":
        return self

    def __exit__(self, exc_type: object, exc: object, traceback: object) -> None:
        try:
            self.shutdown()
        except CanBusError:
            if exc_type is None:
                raise


def open_bus(
    interface: str, channel: str, *, send_timeout: float = 1.0
) -> PythonCanBus:
    try:
        import can
    except ImportError as exc:
        raise CanBusError(
            "python-can is not installed; install Tools/canloader before using the CLI"
        ) from exc

    try:
        native_bus = can.Bus(interface=interface, channel=channel)
    except Exception as exc:
        raise CanBusError(
            f"could not open channel {channel!r} with interface {interface!r}: {exc}. "
            "Configure the CAN interface separately; CanLoader does not run sudo."
        ) from exc
    return PythonCanBus(native_bus, can, send_timeout)
