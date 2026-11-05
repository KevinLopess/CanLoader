#!/usr/bin/env python3
"""Extract the useful transfer throughput from timestamped canloader logs.

Reads logs produced by ``run_timestamped_canloader.py ... --verbose`` and
measures the interval between the START_UPDATE command sent by the tool and the
ACK of the VERIFY command received from the target, i.e. from the start of the
update to the successful response of the final verification. The interval
includes the session setup, the Flash erase, the block transfer, FINALIZE and
VERIFY. The image size must be given per log, because the log does not record
it in a single line.

Host timestamps include adapter latency; they are not on-chip measurements.
"""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

LINE = re.compile(
    r"^(?P<ts>\d+\.\d+) DEBUG: (?P<dir>TX|RX) (?P<id>[0-9A-F]{8}) \[\d\] (?P<data>[0-9a-f ]+)$"
)
PF_PROPRIETARY_A = 0xEF
START_UPDATE = 0x10
VERIFY = 0x31
ACK = 0x80


def analyze(path: Path, size: int) -> dict | None:
    start = verified = None
    for raw in path.read_text(errors="replace").splitlines():
        match = LINE.match(raw.strip())
        if not match:
            continue
        can_id = int(match["id"], 16)
        if (can_id >> 16) & 0xFF != PF_PROPRIETARY_A:
            continue
        data = bytes.fromhex(match["data"])
        if len(data) < 3:
            continue
        ts = float(match["ts"])
        if match["dir"] == "TX" and data[1] == START_UPDATE and start is None:
            start = ts
        elif match["dir"] == "RX" and data[1] == ACK and data[2] == VERIFY:
            verified = ts
    if start is None or verified is None:
        return None
    interval = verified - start
    return {
        "size_bytes": size,
        "start_to_verify_s": round(interval, 3),
        "throughput_bytes_per_s": round(size / interval),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "entries",
        nargs="+",
        help="LOG=SIZE pairs, where SIZE is the image size in bytes",
    )
    args = parser.parse_args()
    report = {}
    for entry in args.entries:
        log, size = entry.rsplit("=", 1)
        report[log] = analyze(Path(log), int(size))
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
