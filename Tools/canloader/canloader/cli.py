"""Command-line interface for the CanLoader J1939 host client."""

import argparse
import logging
from pathlib import Path
import sys

from . import __version__
from .bus import open_bus
from .client import BootloaderClient, NetworkScanner
from .constants import (
    DEFAULT_SOURCE_ADDRESS,
    DEFAULT_SOURCE_NAME,
    DEFAULT_TARGET_ADDRESS,
    PROTOCOL_VERSION,
    InfoSelector,
)
from .errors import CanLoaderError


def _auto_int(value: str) -> int:
    return int(value, 0)


def _add_bus_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--interface", default="socketcan", help="python-can backend")
    parser.add_argument("--channel", default="can0", help="CAN channel (default: can0)")
    parser.add_argument(
        "--bitrate",
        type=_auto_int,
        default=500000,
        help="CAN bitrate passed to serial CAN backends (default: 500000)",
    )
    parser.add_argument(
        "--tty-baudrate",
        type=_auto_int,
        default=None,
        help="optional serial bitrate for SLCAN/SeeedStudio adapters",
    )
    parser.add_argument(
        "--source-address",
        type=_auto_int,
        default=DEFAULT_SOURCE_ADDRESS,
        help="J1939 source address used by this service tool (default: 0xF9)",
    )
    parser.add_argument(
        "--source-name",
        type=_auto_int,
        default=DEFAULT_SOURCE_NAME,
        help="64-bit J1939 NAME used by this service tool",
    )
    parser.add_argument(
        "--claim-timeout",
        type=float,
        default=0.250,
        help="address-claim arbitration window in seconds (default: 0.250)",
    )
    parser.add_argument("--verbose", action="store_true", help="show CAN frame log")


def _common_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(add_help=False)
    _add_bus_arguments(parser)
    parser.add_argument(
        "--address",
        type=_auto_int,
        default=DEFAULT_TARGET_ADDRESS,
        help="target J1939 source address (default: 0x80)",
    )
    parser.add_argument(
        "--name",
        type=_auto_int,
        default=None,
        help="expected target J1939 NAME; locks identity across the operation",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=0.5,
        help="control and DM15 timeout in seconds (default: 0.5)",
    )
    parser.add_argument(
        "--scan-timeout",
        type=float,
        default=0.2,
        help="Address Claimed collection window in seconds (default: 0.2)",
    )
    parser.add_argument(
        "--erase-timeout",
        type=float,
        default=4.0,
        help="erase/reset response timeout in seconds (default: 4)",
    )
    parser.add_argument(
        "--transport-timeout",
        type=float,
        default=1.5,
        help="per-step J1939 TP timeout in seconds (default: 1.5)",
    )
    parser.add_argument(
        "--retries",
        type=int,
        default=3,
        help="retransmissions after the initial attempt (default: 3)",
    )
    return parser


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="canloader", description="Update STM32 firmware over J1939 CAN"
    )
    parser.add_argument("--version", action="version", version=__version__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    common = _common_parser()

    scan = subparsers.add_parser(
        "ls", help="list J1939 NAME/address claims and identify CanLoader devices"
    )
    _add_bus_arguments(scan)
    scan.add_argument(
        "--scan-timeout",
        type=float,
        default=0.2,
        help="Address Claimed collection window in seconds (default: 0.2)",
    )
    scan.add_argument(
        "--identify-timeout",
        type=float,
        default=0.2,
        help="directed CanLoader IDENTIFY timeout (default: 0.2)",
    )

    subparsers.add_parser("info", parents=[common], help="read bootloader information")

    update = subparsers.add_parser(
        "update", parents=[common], help="transfer and verify a firmware image"
    )
    update.add_argument("firmware", type=Path, help="raw application .bin file")
    update.add_argument(
        "--run", action="store_true", help="run the application after verification"
    )

    subparsers.add_parser(
        "verify", parents=[common], help="ask the target to verify its stored image"
    )
    subparsers.add_parser(
        "run", parents=[common], help="run the currently valid application"
    )
    subparsers.add_parser(
        "reboot", parents=[common], help="ask the application to enter the bootloader"
    )
    set_address = subparsers.add_parser(
        "set-address",
        parents=[common],
        help="send J1939 Commanded Address to the locked target NAME",
    )
    set_address.add_argument(
        "new_address", type=_auto_int, help="new J1939 address (0x00..0xFD)"
    )
    return parser


class _Progress:
    def __call__(self, committed: int, total: int) -> None:
        percent = (committed * 100.0 / total) if total else 100.0
        end = "\n" if committed >= total else ""
        print(
            f"\rTransfer: {committed}/{total} bytes ({percent:5.1f}%)",
            end=end,
            file=sys.stderr,
            flush=True,
        )


def _client_from_args(bus: object, args: argparse.Namespace) -> BootloaderClient:
    return BootloaderClient(
        bus,  # type: ignore[arg-type]
        address=args.address,
        target_name=args.name,
        source_address=args.source_address,
        source_name=args.source_name,
        timeout=args.timeout,
        erase_timeout=args.erase_timeout,
        claim_timeout=args.claim_timeout,
        scan_timeout=args.scan_timeout,
        transport_timeout=args.transport_timeout,
        retries=args.retries,
    )


def _open_from_args(args: argparse.Namespace, send_timeout: float):
    is_serial_can = args.interface.lower() in {"slcan", "seeedstudio"}
    return open_bus(
        args.interface,
        args.channel,
        send_timeout=send_timeout,
        bitrate=args.bitrate if is_serial_can else None,
        tty_baudrate=args.tty_baudrate if is_serial_can else None,
    )


def _mode(device: object) -> str:
    if getattr(device, "capabilities", None) is None:
        return "j1939"
    if getattr(device, "application_running"):
        return "application"
    if getattr(device, "update_active"):
        return "update"
    return "bootloader"


def _run_command(args: argparse.Namespace) -> None:
    image: bytes | None = None
    if args.command == "update":
        try:
            image = args.firmware.read_bytes()
        except OSError as exc:
            raise CanLoaderError(
                f"could not read firmware {args.firmware}: {exc}"
            ) from exc

    send_timeout = args.scan_timeout if args.command == "ls" else args.timeout
    with _open_from_args(args, send_timeout) as bus:
        if args.command == "ls":
            devices = NetworkScanner(
                bus,
                timeout=args.scan_timeout,
                identify_timeout=args.identify_timeout,
                source_address=args.source_address,
                source_name=args.source_name,
                claim_timeout=args.claim_timeout,
            ).scan()
            if not devices:
                print("No J1939 devices found.")
                return
            print("ADDR  NAME                MFG   FUNC  CANLOADER  MODE")
            for device in devices:
                canloader = "yes" if device.is_canloader else "no"
                print(
                    f"0x{device.address:02X}  {device.name}  "
                    f"{device.name.manufacturer_code:>4}  "
                    f"{device.name.function:>4}  {canloader:<9}  {_mode(device)}"
                )
            return

        client = _client_from_args(bus, args)
        if args.command == "info":
            info = client.read_device_info()
            print(f"Protocol version: {PROTOCOL_VERSION}")
            print(f"Address: 0x{info.address:02X}")
            print(f"NAME: {info.name}")
            print(f"Capabilities: 0x{info.capabilities:08X}")
            print(f"Application base: 0x{info.app_base:08X}")
            print(f"Application maximum size: {info.app_max_size} bytes")
            print(f"Stored image size: {info.stored_size} bytes")
            print(f"Stored image CRC: 0x{info.stored_crc:08X}")
            print(f"Device UID: {info.uid}")
        elif args.command == "update":
            assert image is not None
            result = client.update(image, run=args.run, progress=_Progress())
            print(
                f"Verified {result.size} bytes on {result.target_name}, "
                f"CRC 0x{result.verified_crc:08X}."
            )
            if result.ran_application:
                print("Application run requested.")
        elif args.command == "verify":
            crc = client.verify()
            print(f"Stored image verified, CRC 0x{crc:08X}.")
        elif args.command == "run":
            stored_crc = client.info(InfoSelector.STORED_CRC)
            client.run_application(stored_crc)
            print("Application run requested.")
        elif args.command == "reboot":
            client.enter_bootloader(wait=True)
            assert client.target_name is not None
            print(f"{client.target_name} entered the bootloader at 0x{client.address:02X}.")
        elif args.command == "set-address":
            old_address = client.address
            client.set_address(args.new_address)
            assert client.target_name is not None
            print(
                f"{client.target_name} changed from 0x{old_address:02X} "
                f"to 0x{args.new_address:02X}."
            )
        else:
            raise AssertionError(f"unhandled command {args.command}")


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.WARNING,
        format="%(levelname)s: %(message)s",
    )
    try:
        _run_command(args)
    except CanLoaderError as exc:
        parser.exit(2, f"canloader: error: {exc}\n")
    except KeyboardInterrupt:
        parser.exit(130, "canloader: interrupted\n")
    return 0
