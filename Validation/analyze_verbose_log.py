#!/usr/bin/env python3
"""Extrai contagens reproduzíveis de um log --verbose do CanLoader."""

from __future__ import annotations

import argparse
import json
import re
from collections import Counter
from pathlib import Path


FRAME_RE = re.compile(
    r"DEBUG: (TX|RX) ([0-9A-Fa-f]{8}) \[(\d+)\]((?: [0-9A-Fa-f]{2})*)"
)
PROGRESS_RE = re.compile(r"Transfer: (\d+)/(\d+) bytes")
VERIFIED_RE = re.compile(
    r"Verified (\d+) bytes on (0x[0-9A-Fa-f]+), CRC (0x[0-9A-Fa-f]+)\."
)
DURATION_RE = re.compile(r"^duration_seconds=([0-9.]+)$", re.MULTILINE)
EXIT_RE = re.compile(r"^exit_code=(-?\d+)$", re.MULTILINE)


def normalized_pgn(can_id: int) -> int:
    pgn = (can_id >> 8) & 0x3FFFF
    pf = (can_id >> 16) & 0xFF
    if pf < 0xF0:
        pgn &= 0x3FF00
    return pgn


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Summarize frames found in a CanLoader --verbose log."
    )
    parser.add_argument("--log", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    source = args.log.resolve()
    if not source.is_file():
        raise SystemExit(f"log does not exist: {source}")

    text = source.read_text(encoding="utf-8", errors="replace")
    by_direction: Counter[str] = Counter()
    by_id: Counter[str] = Counter()
    by_pgn: Counter[str] = Counter()
    by_source: Counter[str] = Counter()
    by_destination: Counter[str] = Counter()
    parsed = 0

    for match in FRAME_RE.finditer(text):
        direction = match.group(1)
        can_id = int(match.group(2), 16)
        dlc = int(match.group(3))
        payload = bytes.fromhex(match.group(4))
        if len(payload) != dlc:
            raise SystemExit(
                f"DLC mismatch at frame {parsed + 1}: {dlc} != {len(payload)}"
            )
        pgn = normalized_pgn(can_id)
        pf = (can_id >> 16) & 0xFF
        ps = (can_id >> 8) & 0xFF
        source_address = can_id & 0xFF
        destination = ps if pf < 0xF0 else 0xFF
        by_direction[direction] += 1
        by_id[f"{direction}:0x{can_id:08X}"] += 1
        by_pgn[f"{direction}:0x{pgn:05X}"] += 1
        by_source[f"{direction}:0x{source_address:02X}"] += 1
        by_destination[f"{direction}:0x{destination:02X}"] += 1
        parsed += 1

    progress = [(int(done), int(total)) for done, total in PROGRESS_RE.findall(text)]
    verified = VERIFIED_RE.findall(text)
    durations = DURATION_RE.findall(text)
    exits = EXIT_RE.findall(text)
    result = {
        "schema_version": 1,
        "source_log": str(args.log),
        "parsed_frames": parsed,
        "frames_by_direction": dict(sorted(by_direction.items())),
        "frames_by_can_id": dict(sorted(by_id.items())),
        "frames_by_pgn": dict(sorted(by_pgn.items())),
        "frames_by_source_address": dict(sorted(by_source.items())),
        "frames_by_destination": dict(sorted(by_destination.items())),
        "last_transfer_progress": (
            {"bytes": progress[-1][0], "total_bytes": progress[-1][1]}
            if progress
            else None
        ),
        "verified_result": (
            {
                "bytes": int(verified[-1][0]),
                "target_name": verified[-1][1],
                "crc32": verified[-1][2],
            }
            if verified
            else None
        ),
        "command_duration_seconds": float(durations[-1]) if durations else None,
        "command_exit_code": int(exits[-1]) if exits else None,
        "limitations": [
            "Frames are limited to those consumed by the CLI.",
            "The verbose log has no per-frame timestamps.",
            "Command duration includes discovery and control outside START_UPDATE-to-VERIFY.",
        ],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(result, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    print(f"analysis written: {args.output} ({parsed} frames)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
