from pathlib import Path

import pytest

import canloader.cli as cli
from canloader.client import DiscoveredDevice
from canloader.j1939 import J1939Name


def test_info_defaults_to_j1939_target_address_0x80() -> None:
    args = cli.build_parser().parse_args(["info"])
    assert args.interface == "socketcan"
    assert args.channel == "can0"
    assert args.address == 0x80
    assert args.name is None
    assert args.source_address == 0xF9
    assert args.bitrate == 500000
    assert args.claim_timeout == 0.250


def test_update_accepts_identity_and_slcan_options() -> None:
    args = cli.build_parser().parse_args(
        [
            "update",
            "firmware.bin",
            "--interface",
            "slcan",
            "--channel",
            "/dev/cu.usbmodem1",
            "--address",
            "0x92",
            "--name",
            "0x8123456789abcdef",
            "--source-address",
            "0xfa",
            "--source-name",
            "0x8000000000000001",
            "--bitrate",
            "250000",
            "--tty-baudrate",
            "115200",
            "--run",
        ]
    )
    assert args.firmware == Path("firmware.bin")
    assert args.interface == "slcan"
    assert args.address == 0x92
    assert args.name == 0x8123456789ABCDEF
    assert args.source_address == 0xFA
    assert args.bitrate == 250000
    assert args.tty_baudrate == 115200
    assert args.run is True


def test_v1_node_and_set_node_cli_are_removed() -> None:
    with pytest.raises(SystemExit):
        cli.build_parser().parse_args(["info", "--node", "1"])
    with pytest.raises(SystemExit):
        cli.build_parser().parse_args(["set-node", "2"])


def test_set_address_command_uses_address_terminology() -> None:
    args = cli.build_parser().parse_args(
        ["set-address", "0x91", "--address", "0x80", "--name", "0x1234"]
    )
    assert args.command == "set-address"
    assert args.address == 0x80
    assert args.new_address == 0x91
    assert args.name == 0x1234


class DummyContextBus:
    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, traceback):
        return None


def test_ls_runs_scanner_and_prints_name_fields(monkeypatch, capsys) -> None:
    opened: list[tuple[object, float]] = []

    def fake_open(args, send_timeout):
        opened.append((args, send_timeout))
        return DummyContextBus()

    class FakeScanner:
        def __init__(self, bus, **kwargs):
            del bus, kwargs

        def scan(self):
            return [
                DiscoveredDevice(
                    address=0x80,
                    name=J1939Name.from_fields(
                        identity_number=1,
                        manufacturer_code=42,
                        function=7,
                    ),
                    capabilities=0xF1020001,
                )
            ]

    monkeypatch.setattr(cli, "_open_from_args", fake_open)
    monkeypatch.setattr(cli, "NetworkScanner", FakeScanner)
    args = cli.build_parser().parse_args(
        ["ls", "--interface", "slcan", "--channel", "/dev/cu.usbmodem1"]
    )
    cli._run_command(args)
    output = capsys.readouterr().out
    assert "ADDR  NAME" in output
    assert "0x80" in output
    assert "yes" in output
    assert opened[0][1] == 0.2


def test_serial_backends_receive_bitrates_but_socketcan_does_not(monkeypatch) -> None:
    calls: list[dict[str, object]] = []

    def fake_open_bus(interface, channel, **kwargs):
        calls.append({"interface": interface, "channel": channel, **kwargs})
        return DummyContextBus()

    monkeypatch.setattr(cli, "open_bus", fake_open_bus)
    slcan = cli.build_parser().parse_args(["info", "--interface", "slcan"])
    cli._open_from_args(slcan, 0.5)
    seeed = cli.build_parser().parse_args(
        [
            "info",
            "--interface",
            "seeedstudio",
            "--tty-baudrate",
            "2000000",
        ]
    )
    cli._open_from_args(seeed, 0.5)
    socketcan = cli.build_parser().parse_args(["info"])
    cli._open_from_args(socketcan, 0.5)
    assert calls[0]["bitrate"] == 500000
    assert calls[1]["bitrate"] == 500000
    assert calls[1]["tty_baudrate"] == 2000000
    assert calls[2]["bitrate"] is None
    assert calls[2]["tty_baudrate"] is None
