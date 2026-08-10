"""Command-line interface for the CanLoader host client."""

import argparse
import logging
from pathlib import Path
import sys

from . import __version__
from .bus import open_bus
from .client import BootloaderClient
from .constants import PROTOCOL_VERSION, InfoSelector
from .errors import CanLoaderError


def _auto_int(value: str) -> int:
    return int(value, 0)


def _common_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--interface", default="socketcan", help="python-can backend")
    parser.add_argument("--channel", default="can0", help="CAN channel (default: can0)")
    parser.add_argument(
        "--node", type=_auto_int, default=1, help="target node ID (default: 1)"
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=0.5,
        help="normal response timeout in seconds (default: 0.5)",
    )
    parser.add_argument(
        "--erase-timeout",
        type=float,
        default=4.0,
        help="erase response timeout in seconds (default: 4)",
    )
    parser.add_argument(
        "--retries",
        type=int,
        default=3,
        help="retransmissions after the initial attempt (default: 3)",
    )
    parser.add_argument("--verbose", action="store_true", help="show CAN frame log")
    return parser


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="canloader", description="Update STM32 firmware over CanLoader CAN"
    )
    parser.add_argument("--version", action="version", version=__version__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    common = _common_parser()

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
        node=args.node,
        timeout=args.timeout,
        erase_timeout=args.erase_timeout,
        retries=args.retries,
    )


def _run_command(args: argparse.Namespace) -> None:
    image: bytes | None = None
    if args.command == "update":
        try:
            image = args.firmware.read_bytes()
        except OSError as exc:
            raise CanLoaderError(
                f"could not read firmware {args.firmware}: {exc}"
            ) from exc

    with open_bus(
        args.interface, args.channel, send_timeout=args.timeout
    ) as bus:
        client = _client_from_args(bus, args)
        if args.command == "info":
            client.discover()
            info = client.read_device_info()
            print(f"Protocol version: {PROTOCOL_VERSION}")
            print(f"Node: {args.node}")
            print(f"Capabilities: 0x{info.capabilities:08X}")
            print(f"Application base: 0x{info.app_base:08X}")
            print(f"Application maximum size: {info.app_max_size} bytes")
            print(f"Stored image size: {info.stored_size} bytes")
            print(f"Stored image CRC: 0x{info.stored_crc:08X}")
        elif args.command == "update":
            assert image is not None
            result = client.update(image, run=args.run, progress=_Progress())
            print(
                f"Verified {result.size} bytes, CRC 0x{result.verified_crc:08X}."
            )
            if result.ran_application:
                print("Application run requested.")
        elif args.command == "verify":
            client.discover()
            crc = client.verify()
            print(f"Stored image verified, CRC 0x{crc:08X}.")
        elif args.command == "run":
            client.discover()
            stored_crc = client.info(InfoSelector.STORED_CRC)
            client.run_application(stored_crc)
            print("Application run requested.")
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
