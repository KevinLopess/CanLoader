#!/usr/bin/env python3
"""Run the canloader CLI with a host timestamp on every log line.

The regular ``--verbose`` log has no per-frame time. This wrapper installs a
logging handler before calling ``canloader.cli.main`` so each TX/RX line starts
with the host wall-clock time in seconds (microsecond resolution). The time is
taken when the tool sends or receives the frame through python-can, so it
includes the USB/SLCAN latency of the adapter.

Usage (the arguments are passed unchanged to the canloader CLI):
    .venv/bin/python Validation/run_timestamped_canloader.py update app.bin \
        --interface slcan --channel /dev/cu.usbmodemXXXX --bitrate 500000 \
        --address 0x80 --verbose
"""

from __future__ import annotations

import logging
import sys

from canloader import cli


class _TimestampFormatter(logging.Formatter):
    def format(self, record: logging.LogRecord) -> str:
        return f"{record.created:.6f} {record.levelname}: {record.getMessage()}"


def main() -> int:
    handler = logging.StreamHandler(sys.stderr)
    handler.setFormatter(_TimestampFormatter())
    root = logging.getLogger()
    root.addHandler(handler)
    root.setLevel(logging.DEBUG if "--verbose" in sys.argv[1:] else logging.WARNING)
    return cli.main(sys.argv[1:])


if __name__ == "__main__":
    raise SystemExit(main())
