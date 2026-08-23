"""Exceptions raised by the CanLoader host tool."""

from typing import Any

from .constants import NACK_DESCRIPTIONS, NackCode


class CanLoaderError(Exception):
    """Base class for expected host-tool failures."""


class ConfigurationError(CanLoaderError):
    """A command-line or client configuration value is invalid."""


class CanBusError(CanLoaderError):
    """The CAN backend could not be opened or used."""


class ProtocolError(CanLoaderError):
    """A frame or response violates the selected protocol profile."""


class AddressClaimError(CanLoaderError):
    """A J1939 source address could not be claimed safely."""


class AddressConflictError(AddressClaimError):
    """More than one NAME currently claims an address."""


class TargetIdentityError(CanLoaderError):
    """The target address no longer belongs to the locked J1939 NAME."""


class ResponseTimeout(CanLoaderError):
    def __init__(self, command: Any, attempts: int = 1) -> None:
        self.command = command
        self.attempts = attempts
        name = getattr(command, "name", None) or (
            f"0x{command:02X}" if isinstance(command, int) else str(command)
        )
        suffix = "attempt" if attempts == 1 else "attempts"
        super().__init__(
            f"timed out waiting for {name} response after {attempts} {suffix}"
        )


class NackError(CanLoaderError):
    def __init__(self, command: Any, value: int) -> None:
        self.command = command
        self.value = value
        try:
            self.code: NackCode | None = NackCode(value)
        except ValueError:
            self.code = None

        command_name = getattr(command, "name", None) or (
            f"0x{int(command):02X}" if isinstance(command, int) else str(command)
        )
        if self.code is None:
            detail = f"unknown device error (code {value})"
        else:
            detail = f"{NACK_DESCRIPTIONS[self.code]} (code {value})"
        super().__init__(f"device rejected {command_name}: {detail}")


class DeviceSessionTimeout(NackError):
    def __init__(self, waiting_for: Any) -> None:
        super().__init__(0x3F, NackCode.TIMEOUT)
        self.waiting_for = waiting_for
        name = getattr(waiting_for, "name", str(waiting_for))
        self.args = (f"device session timed out while waiting for {name}",)


class VerificationError(CanLoaderError):
    """The CRC returned by VERIFY differs from the transmitted image CRC."""


class BlockTransferError(CanLoaderError):
    """A block could not be committed after all retry attempts."""


class TransportError(CanLoaderError):
    """A J1939 transport-protocol connection failed."""


class TransportAbortError(TransportError):
    def __init__(self, reason: int, pgn: int) -> None:
        self.reason = reason
        self.pgn = pgn
        super().__init__(
            f"peer aborted transport for PGN 0x{pgn:05X} (reason {reason})"
        )
