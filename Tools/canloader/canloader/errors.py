"""Exceptions raised by the CanLoader host tool."""

from .constants import Command, NACK_DESCRIPTIONS, NackCode


class CanLoaderError(Exception):
    """Base class for expected host-tool failures."""


class ConfigurationError(CanLoaderError):
    """A command-line or client configuration value is invalid."""


class CanBusError(CanLoaderError):
    """The CAN backend could not be opened or used."""


class ProtocolError(CanLoaderError):
    """A frame or response violates the CanLoader protocol."""


class ResponseTimeout(CanLoaderError):
    def __init__(self, command: Command | int, attempts: int = 1) -> None:
        self.command = command
        self.attempts = attempts
        name = command.name if isinstance(command, Command) else f"0x{command:02X}"
        suffix = "attempt" if attempts == 1 else "attempts"
        super().__init__(
            f"timed out waiting for {name} response after {attempts} {suffix}"
        )


class NackError(CanLoaderError):
    def __init__(self, command: Command | int, value: int) -> None:
        self.command = command
        self.value = value
        try:
            self.code: NackCode | None = NackCode(value)
        except ValueError:
            self.code = None

        command_name = (
            command.name if isinstance(command, Command) else f"0x{command:02X}"
        )
        if self.code is None:
            detail = f"unknown device error (code {value})"
        else:
            detail = f"{NACK_DESCRIPTIONS[self.code]} (code {value})"
        super().__init__(f"device rejected {command_name}: {detail}")


class DeviceSessionTimeout(NackError):
    def __init__(self, waiting_for: Command) -> None:
        super().__init__(Command.ABORT, NackCode.TIMEOUT)
        self.waiting_for = waiting_for
        self.args = (
            f"device session timed out while waiting for {waiting_for.name} "
            f"(asynchronous ABORT NACK, code {NackCode.TIMEOUT.value})",
        )


class VerificationError(CanLoaderError):
    """The CRC returned by VERIFY differs from the transmitted image CRC."""


class BlockTransferError(CanLoaderError):
    """A block could not be committed after all retry attempts."""
