from pathlib import Path

from canloader.cli import build_parser


def test_info_command_defaults_to_socketcan_can0_node_one() -> None:
    args = build_parser().parse_args(["info"])
    assert args.interface == "socketcan"
    assert args.channel == "can0"
    assert args.node == 1
    assert args.timeout == 0.5
    assert args.erase_timeout == 4.0
    assert args.retries == 3


def test_update_accepts_all_transport_options_and_run() -> None:
    args = build_parser().parse_args(
        [
            "update",
            "firmware.bin",
            "--interface",
            "virtual",
            "--channel",
            "test",
            "--node",
            "0x2A",
            "--timeout",
            "1.25",
            "--erase-timeout",
            "20",
            "--retries",
            "5",
            "--verbose",
            "--run",
        ]
    )
    assert args.firmware == Path("firmware.bin")
    assert args.interface == "virtual"
    assert args.channel == "test"
    assert args.node == 0x2A
    assert args.timeout == 1.25
    assert args.erase_timeout == 20.0
    assert args.retries == 5
    assert args.verbose is True
    assert args.run is True


def test_verify_and_run_commands_are_available() -> None:
    assert build_parser().parse_args(["verify"]).command == "verify"
    assert build_parser().parse_args(["run"]).command == "run"
