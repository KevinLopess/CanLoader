"""Adapter between the protocol's small bus API and python-can."""

import math
import time
from types import ModuleType
from typing import Any, Protocol

from .errors import CanBusError
from .j1939 import CanFrame


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
            is_remote_frame=frame.is_remote_frame,
            is_error_frame=frame.is_error_frame,
        )
        try:
            self._native_bus.send(message, timeout=self._send_timeout)
        except Exception as exc:
            raise CanBusError(f"failed to send CAN frame: {exc}") from exc

    def recv(self, timeout: float) -> CanFrame | None:
        if not math.isfinite(timeout) or timeout < 0:
            raise CanBusError("receive timeout must be finite and non-negative")
        deadline = time.monotonic() + timeout
        while True:
            remaining = max(0.0, deadline - time.monotonic())
            try:
                message = self._native_bus.recv(remaining)
            except Exception as exc:
                raise CanBusError(f"failed to receive CAN frame: {exc}") from exc
            if message is None:
                return None
            # CanLoader v2 is exclusively J1939 data traffic. Filtering here
            # keeps backend-specific RTR/error frames out of all state machines.
            if (
                not bool(getattr(message, "is_extended_id", False))
                or bool(getattr(message, "is_remote_frame", False))
                or bool(getattr(message, "is_error_frame", False))
            ):
                if time.monotonic() >= deadline:
                    return None
                continue
            return CanFrame(
                arbitration_id=int(message.arbitration_id),
                data=bytes(message.data),
                is_extended_id=True,
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
    interface: str,
    channel: str,
    *,
    send_timeout: float = 1.0,
    bitrate: int | None = None,
    tty_baudrate: int | None = None,
) -> PythonCanBus:
    try:
        import can
    except ImportError as exc:
        raise CanBusError(
            "python-can is not installed; install Tools/canloader before using the CLI"
        ) from exc

    if bitrate is not None and bitrate <= 0:
        raise CanBusError("CAN bitrate must be greater than zero")
    if tty_baudrate is not None and tty_baudrate <= 0:
        raise CanBusError("serial baudrate must be greater than zero")

    normalized_interface = interface.lower()
    if tty_baudrate is not None and normalized_interface == "slcan":
        if "@" in channel:
            raise CanBusError(
                "SLCAN channel already contains a serial bitrate; "
                "do not also use --tty-baudrate"
            )
        # Both python-can 4.3 (ttyBaudrate) and 4.5+ (tty_baudrate)
        # support the backend-independent channel@baud form.
        channel = f"{channel}@{tty_baudrate}"
    elif tty_baudrate is not None and normalized_interface not in {"seeedstudio"}:
        raise CanBusError(
            "serial baudrate is supported only by SLCAN/SeeedStudio backends"
        )

    options: dict[str, object] = {"interface": interface, "channel": channel}
    if bitrate is not None:
        options["bitrate"] = bitrate
    if normalized_interface == "seeedstudio":
        # This backend defaults to standard frames unless explicitly told to
        # use its extended-frame mode.
        options["frame_type"] = "EXT"
        if tty_baudrate is not None:
            options["baudrate"] = tty_baudrate
    try:
        native_bus = can.Bus(**options)
    except Exception as exc:
        raise CanBusError(
            f"could not open channel {channel!r} with interface {interface!r}: {exc}. "
            "Configure the CAN interface separately; CanLoader does not run sudo."
        ) from exc
    return PythonCanBus(native_bus, can, send_timeout)
