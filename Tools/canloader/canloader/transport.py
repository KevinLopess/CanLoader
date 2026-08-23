"""J1939-21 transport protocol implemented over the small raw CAN bus API."""

from dataclasses import dataclass
import logging
import math
import time
from collections.abc import Callable

from .bus import CanBus
from .constants import (
    DEFAULT_PRIORITY,
    GLOBAL_ADDRESS,
    MAX_TP_PACKETS,
    MAX_TP_PAYLOAD,
    PGN_ADDRESS_CLAIMED,
    PGN_TP_CM,
    PGN_TP_DT,
    TP_DATA_BYTES_PER_PACKET,
    TpAbortReason,
    TpControl,
)
from .errors import (
    CanBusError,
    ConfigurationError,
    ProtocolError,
    TransportAbortError,
    TransportError,
)
from .j1939 import CanFrame, identifier_from_frame, make_frame, validate_unicast_address


LOGGER = logging.getLogger(__name__)
FrameObserver = Callable[[CanFrame], None]


@dataclass(frozen=True, slots=True)
class TpCm:
    control: TpControl
    pgn: int
    total_size: int | None = None
    packet_count: int | None = None
    packets_per_cts: int | None = None
    next_packet: int | None = None
    reason: int | None = None


def _validate_tp_size(payload: bytes) -> int:
    if not 9 <= len(payload) <= MAX_TP_PAYLOAD:
        raise ConfigurationError(
            f"transport payload must contain 9..{MAX_TP_PAYLOAD} bytes"
        )
    return math.ceil(len(payload) / TP_DATA_BYTES_PER_PACKET)


def _pgn_bytes(pgn: int) -> bytes:
    if not 0 <= pgn <= 0x1FFFF:
        raise ConfigurationError("transported PGN must be an unsigned 17-bit value")
    return pgn.to_bytes(3, "little")


def encode_tp_rts(
    source_address: int,
    destination_address: int,
    pgn: int,
    total_size: int,
    packet_count: int,
    packets_per_cts: int,
) -> CanFrame:
    validate_unicast_address(source_address, "source address")
    validate_unicast_address(destination_address, "destination address")
    if not 9 <= total_size <= MAX_TP_PAYLOAD:
        raise ConfigurationError("RTS total size is outside the TP range")
    expected_packets = math.ceil(total_size / TP_DATA_BYTES_PER_PACKET)
    if packet_count != expected_packets:
        raise ConfigurationError("RTS packet count does not match total size")
    if not 1 <= packets_per_cts <= MAX_TP_PACKETS:
        raise ConfigurationError("RTS packets-per-CTS must be 1..255")
    data = bytes(
        (
            TpControl.RTS,
            total_size & 0xFF,
            total_size >> 8,
            packet_count,
            packets_per_cts,
        )
    ) + _pgn_bytes(pgn)
    return make_frame(
        PGN_TP_CM,
        source_address,
        data,
        destination_address=destination_address,
    )


def encode_tp_cts(
    source_address: int,
    destination_address: int,
    pgn: int,
    packet_count: int,
    next_packet: int,
) -> CanFrame:
    if not 0 <= packet_count <= MAX_TP_PACKETS:
        raise ConfigurationError("CTS packet count must be 0..255")
    if not 1 <= next_packet <= MAX_TP_PACKETS:
        raise ConfigurationError("CTS next packet must be 1..255")
    data = bytes((TpControl.CTS, packet_count, next_packet, 0xFF, 0xFF))
    data += _pgn_bytes(pgn)
    return make_frame(
        PGN_TP_CM,
        source_address,
        data,
        destination_address=destination_address,
    )


def encode_tp_eom_ack(
    source_address: int,
    destination_address: int,
    pgn: int,
    total_size: int,
    packet_count: int,
) -> CanFrame:
    if not 9 <= total_size <= MAX_TP_PAYLOAD:
        raise ConfigurationError("EOM_ACK total size is outside the TP range")
    if packet_count != math.ceil(total_size / TP_DATA_BYTES_PER_PACKET):
        raise ConfigurationError("EOM_ACK packet count does not match total size")
    data = bytes(
        (
            TpControl.EOM_ACK,
            total_size & 0xFF,
            total_size >> 8,
            packet_count,
            0xFF,
        )
    ) + _pgn_bytes(pgn)
    return make_frame(
        PGN_TP_CM,
        source_address,
        data,
        destination_address=destination_address,
    )


def encode_tp_bam(
    source_address: int,
    pgn: int,
    total_size: int,
    packet_count: int,
) -> CanFrame:
    if not 9 <= total_size <= MAX_TP_PAYLOAD:
        raise ConfigurationError("BAM total size is outside the TP range")
    if packet_count != math.ceil(total_size / TP_DATA_BYTES_PER_PACKET):
        raise ConfigurationError("BAM packet count does not match total size")
    data = bytes(
        (
            TpControl.BAM,
            total_size & 0xFF,
            total_size >> 8,
            packet_count,
            0xFF,
        )
    ) + _pgn_bytes(pgn)
    return make_frame(
        PGN_TP_CM,
        source_address,
        data,
        destination_address=GLOBAL_ADDRESS,
    )


def encode_tp_abort(
    source_address: int,
    destination_address: int,
    pgn: int,
    reason: TpAbortReason | int,
) -> CanFrame:
    reason_value = int(reason)
    if not 0 <= reason_value <= 0xFF:
        raise ConfigurationError("TP abort reason must be an unsigned byte")
    data = bytes((TpControl.ABORT, reason_value, 0xFF, 0xFF, 0xFF))
    data += _pgn_bytes(pgn)
    return make_frame(
        PGN_TP_CM,
        source_address,
        data,
        destination_address=destination_address,
    )


def encode_tp_dt(
    source_address: int,
    destination_address: int,
    sequence: int,
    payload: bytes | bytearray | memoryview,
) -> CanFrame:
    if not 1 <= sequence <= MAX_TP_PACKETS:
        raise ConfigurationError("TP.DT sequence must be 1..255")
    chunk = bytes(payload)
    if not 1 <= len(chunk) <= TP_DATA_BYTES_PER_PACKET:
        raise ConfigurationError("TP.DT data must contain 1..7 bytes")
    return make_frame(
        PGN_TP_DT,
        source_address,
        bytes((sequence,)) + chunk.ljust(TP_DATA_BYTES_PER_PACKET, b"\xFF"),
        destination_address=destination_address,
    )


def decode_tp_cm(frame: CanFrame) -> tuple[object, TpCm]:
    identifier = identifier_from_frame(frame)
    if identifier.pgn != PGN_TP_CM:
        raise ProtocolError("frame is not TP.CM")
    if frame.dlc != 8:
        raise ProtocolError("TP.CM frame must contain exactly 8 bytes")
    try:
        control = TpControl(frame.data[0])
    except ValueError as exc:
        raise ProtocolError(f"unknown TP.CM control 0x{frame.data[0]:02X}") from exc
    pgn = int.from_bytes(frame.data[5:8], "little")
    if control in (TpControl.RTS, TpControl.BAM, TpControl.EOM_ACK):
        total_size = int.from_bytes(frame.data[1:3], "little")
        packet_count = frame.data[3]
        expected = math.ceil(total_size / TP_DATA_BYTES_PER_PACKET)
        if not 9 <= total_size <= MAX_TP_PAYLOAD or packet_count != expected:
            raise ProtocolError("TP.CM size and packet count are inconsistent")
        if control is TpControl.RTS and frame.data[4] == 0:
            raise ProtocolError("TP.RTS packets-per-CTS cannot be zero")
        if control in (TpControl.BAM, TpControl.EOM_ACK) and frame.data[4] != 0xFF:
            raise ProtocolError("TP.CM reserved byte must be 0xFF")
        return identifier, TpCm(
            control,
            pgn,
            total_size=total_size,
            packet_count=packet_count,
            packets_per_cts=frame.data[4] if control is TpControl.RTS else None,
        )
    if control is TpControl.CTS:
        if frame.data[3:5] != b"\xFF\xFF":
            raise ProtocolError("TP.CTS reserved bytes must be 0xFF")
        return identifier, TpCm(
            control,
            pgn,
            packet_count=frame.data[1],
            next_packet=frame.data[2],
        )
    if frame.data[2:5] != b"\xFF\xFF\xFF":
        raise ProtocolError("TP.ABORT reserved bytes must be 0xFF")
    return identifier, TpCm(control, pgn, reason=frame.data[1])


class J1939Transport:
    """Sends direct, BAM and destination-specific RTS/CTS messages."""

    def __init__(
        self,
        bus: CanBus,
        *,
        source_address: int,
        timeout: float = 0.5,
        packets_per_cts: int = 16,
        bam_interval: float = 0.05,
        sleep: Callable[[float], None] = time.sleep,
        frame_observer: FrameObserver | None = None,
    ) -> None:
        validate_unicast_address(source_address, "source address")
        if not math.isfinite(timeout) or timeout <= 0:
            raise ConfigurationError("transport timeout must be greater than zero")
        if not 1 <= packets_per_cts <= MAX_TP_PACKETS:
            raise ConfigurationError("packets per CTS must be 1..255")
        if not math.isfinite(bam_interval) or bam_interval < 0:
            raise ConfigurationError("BAM interval cannot be negative")
        self.bus = bus
        self.source_address = source_address
        self.timeout = timeout
        self.packets_per_cts = packets_per_cts
        self.bam_interval = bam_interval
        self.sleep = sleep
        self.frame_observer = frame_observer

    def _send(self, frame: CanFrame) -> None:
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

    def _recv(self, timeout: float) -> CanFrame | None:
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

    def _wait_cm(self, peer: int, pgn: int) -> TpCm:
        deadline = time.monotonic() + self.timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TransportError(
                    f"timed out waiting for TP.CM for PGN 0x{pgn:05X}"
                )
            frame = self._recv(remaining)
            if frame is None:
                raise TransportError(
                    f"timed out waiting for TP.CM for PGN 0x{pgn:05X}"
                )
            try:
                identifier = identifier_from_frame(frame)
            except ProtocolError:
                continue
            if identifier.pgn == PGN_ADDRESS_CLAIMED and self.frame_observer:
                self.frame_observer(frame)
                continue
            if (
                identifier.pgn != PGN_TP_CM
                or identifier.source_address != peer
                or identifier.destination_address != self.source_address
            ):
                if self.frame_observer:
                    self.frame_observer(frame)
                continue
            try:
                _, message = decode_tp_cm(frame)
            except ProtocolError:
                continue
            if message.pgn != pgn:
                continue
            if message.control is TpControl.ABORT:
                raise TransportAbortError(message.reason or 0, pgn)
            return message

    def _abort(self, peer: int, pgn: int, reason: TpAbortReason) -> None:
        try:
            self._send(encode_tp_abort(self.source_address, peer, pgn, reason))
        except Exception:
            LOGGER.debug("best-effort TP.ABORT could not be sent", exc_info=True)

    def send_direct(self, pgn: int, destination_address: int, payload: bytes) -> None:
        if len(payload) > 8:
            raise ConfigurationError("direct J1939 payload cannot exceed 8 bytes")
        self._send(
            make_frame(
                pgn,
                self.source_address,
                payload,
                destination_address=destination_address,
                priority=DEFAULT_PRIORITY,
            )
        )

    def send_bam(self, pgn: int, payload: bytes | bytearray | memoryview) -> None:
        data = bytes(payload)
        packet_count = _validate_tp_size(data)
        self._send(encode_tp_bam(self.source_address, pgn, len(data), packet_count))
        for sequence, offset in enumerate(
            range(0, len(data), TP_DATA_BYTES_PER_PACKET), start=1
        ):
            if self.bam_interval:
                self.sleep(self.bam_interval)
            self._send(
                encode_tp_dt(
                    self.source_address,
                    GLOBAL_ADDRESS,
                    sequence,
                    data[offset : offset + TP_DATA_BYTES_PER_PACKET],
                )
            )

    def send_rts_cts(
        self,
        pgn: int,
        destination_address: int,
        payload: bytes | bytearray | memoryview,
    ) -> None:
        validate_unicast_address(destination_address, "destination address")
        data = bytes(payload)
        packet_count = _validate_tp_size(data)
        self._send(
            encode_tp_rts(
                self.source_address,
                destination_address,
                pgn,
                len(data),
                packet_count,
                min(self.packets_per_cts, packet_count),
            )
        )
        try:
            while True:
                message = self._wait_cm(destination_address, pgn)
                if message.control is TpControl.EOM_ACK:
                    if (
                        message.total_size != len(data)
                        or message.packet_count != packet_count
                    ):
                        raise TransportError("TP EOM_ACK does not match the transfer")
                    return
                if message.control is not TpControl.CTS:
                    raise TransportError(
                        f"unexpected {message.control.name} while waiting for CTS"
                    )
                count = message.packet_count
                first = message.next_packet
                assert count is not None and first is not None
                if count == 0:
                    continue
                last = first + count - 1
                if first < 1 or last > packet_count:
                    raise TransportError("CTS requested packets outside the transfer")
                for sequence in range(first, last + 1):
                    offset = (sequence - 1) * TP_DATA_BYTES_PER_PACKET
                    self._send(
                        encode_tp_dt(
                            self.source_address,
                            destination_address,
                            sequence,
                            data[offset : offset + TP_DATA_BYTES_PER_PACKET],
                        )
                    )
        except TransportAbortError:
            raise
        except BaseException:
            self._abort(destination_address, pgn, TpAbortReason.OTHER)
            raise

    def send_message(
        self,
        pgn: int,
        destination_address: int,
        payload: bytes | bytearray | memoryview,
    ) -> None:
        data = bytes(payload)
        if len(data) <= 8:
            self.send_direct(pgn, destination_address, data)
        elif destination_address == GLOBAL_ADDRESS:
            self.send_bam(pgn, data)
        else:
            self.send_rts_cts(pgn, destination_address, data)
