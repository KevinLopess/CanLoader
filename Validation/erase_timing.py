#!/usr/bin/env python3
"""Extract the Flash erase duration from timestamped canloader logs.

Reads logs produced by ``run_timestamped_canloader.py ... --verbose`` and,
for each DM14 ERASE request sent by the tool, reports:

- pages: page count requested in the DM14 length field;
- proceed_to_done_ms: time between the DM15 PROCEED and the DM15
  OPERATION_COMPLETED received from the target. The bootloader starts erasing
  only after PROCEED has left its transmit queue, and the interval also covers
  the metadata page (one extra page erase plus its write);
- request_to_done_ms: time between the DM14 ERASE and the completion.

Host timestamps include adapter latency; they are not on-chip measurements.
"""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

LINE = re.compile(
    r"^(?P<ts>\d+\.\d+) DEBUG: (?P<dir>TX|RX) (?P<id>[0-9A-F]{8}) \[(?P<dlc>\d)\] (?P<data>[0-9a-f ]*)$"
)
PF_DM14 = 0xD9
PF_DM15 = 0xD8
DM14_ERASE = 0
DM15_PROCEED = 0
DM15_COMPLETED = 4


def analyze(path: Path) -> list[dict]:
    results: list[dict] = []
    current: dict | None = None
    for raw in path.read_text(errors="replace").splitlines():
        match = LINE.match(raw.strip())
        if not match:
            continue
        ts = float(match["ts"])
        can_id = int(match["id"], 16)
        pf = (can_id >> 16) & 0xFF
        data = bytes.fromhex(match["data"]) if match["data"].strip() else b""
        if len(data) < 2:
            continue
        if match["dir"] == "TX" and pf == PF_DM14:
            command = (data[1] >> 1) & 0x07
            if command == DM14_ERASE:
                length = data[0] | ((data[1] >> 5) << 8)
                current = {"pages": length, "request": ts, "proceed": None, "done": None}
            continue
        if current is None or match["dir"] != "RX" or pf != PF_DM15:
            continue
        status = (data[1] >> 1) & 0x07
        if status == DM15_PROCEED and current["proceed"] is None:
            current["proceed"] = ts
        elif status == DM15_COMPLETED and current["proceed"] is not None:
            current["done"] = ts
            results.append(
                {
                    "pages": current["pages"],
                    "proceed_to_done_ms": round((ts - current["proceed"]) * 1000, 1),
                    "request_to_done_ms": round((ts - current["request"]) * 1000, 1),
                }
            )
            current = None
    return results


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("logs", nargs="+", type=Path)
    args = parser.parse_args()
    report = {str(path): analyze(path) for path in args.logs}
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
