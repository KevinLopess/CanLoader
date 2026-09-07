#!/usr/bin/env python3
"""Executa um comando explícito e arquiva sua saída e metadados."""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from datetime import datetime
from pathlib import Path
from time import monotonic


def timestamp() -> datetime:
    return datetime.now().astimezone()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Execute one validation command and archive its evidence."
    )
    parser.add_argument(
        "--session",
        required=True,
        type=Path,
        help="existing validation session directory",
    )
    parser.add_argument(
        "--test",
        required=True,
        help="short identifier used in evidence filenames",
    )
    parser.add_argument(
        "command",
        nargs=argparse.REMAINDER,
        help="command and arguments after --",
    )
    args = parser.parse_args()
    if args.command and args.command[0] == "--":
        args.command = args.command[1:]
    if not args.command:
        parser.error("a command is required after --")
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]{0,63}", args.test):
        parser.error("--test must contain only letters, digits, dot, dash or underscore")
    return args


def main() -> int:
    args = parse_args()
    session = args.session.resolve()
    if not session.is_dir():
        print(f"session directory does not exist: {session}", file=sys.stderr)
        return 2

    commands_dir = session / "commands"
    commands_dir.mkdir(exist_ok=True)
    started = timestamp()
    prefix = f"{started.strftime('%Y%m%dT%H%M%S%z')}-{args.test}"
    log_path = commands_dir / f"{prefix}.log"
    metadata_path = commands_dir / f"{prefix}.json"
    if log_path.exists() or metadata_path.exists():
        print(f"evidence prefix already exists: {prefix}", file=sys.stderr)
        return 2

    started_clock = monotonic()
    exit_code = 125
    interrupted = False
    spawn_error = None
    with log_path.open("x", encoding="utf-8") as log_file:
        header = (
            f"started_at={started.isoformat()}\n"
            f"cwd={Path.cwd()}\n"
            f"command_json={json.dumps(args.command, ensure_ascii=False)}\n\n"
        )
        log_file.write(header)
        log_file.flush()
        sys.stdout.write(header)
        sys.stdout.flush()

        try:
            process = subprocess.Popen(
                args.command,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                encoding="utf-8",
                errors="replace",
                bufsize=1,
            )
        except OSError as error:
            spawn_error = f"{type(error).__name__}: {error}"
            message = f"command_start_error={spawn_error}\n"
            log_file.write(message)
            sys.stdout.write(message)
            exit_code = 127
        else:
            try:
                assert process.stdout is not None
                for line in process.stdout:
                    log_file.write(line)
                    log_file.flush()
                    sys.stdout.write(line)
                    sys.stdout.flush()
                exit_code = process.wait()
            except KeyboardInterrupt:
                interrupted = True
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
                exit_code = 130

        ended = timestamp()
        duration = monotonic() - started_clock
        footer = (
            f"\nended_at={ended.isoformat()}\n"
            f"duration_seconds={duration:.6f}\n"
            f"exit_code={exit_code}\n"
        )
        log_file.write(footer)
        sys.stdout.write(footer)

    metadata = {
        "schema_version": 1,
        "test": args.test,
        "command": args.command,
        "cwd": os.fspath(Path.cwd()),
        "started_at": started.isoformat(),
        "ended_at": ended.isoformat(),
        "duration_seconds": round(duration, 6),
        "exit_code": exit_code,
        "interrupted": interrupted,
        "spawn_error": spawn_error,
        "log": os.fspath(log_path.relative_to(session)),
    }
    metadata_path.write_text(
        json.dumps(metadata, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
