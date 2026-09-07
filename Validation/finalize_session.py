#!/usr/bin/env python3
"""Gera um manifesto SHA-256 determinístico para uma sessão de validação."""

from __future__ import annotations

import argparse
import hashlib
import os
import sys
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Create manifest.sha256 for a validation session."
    )
    parser.add_argument(
        "--session",
        required=True,
        type=Path,
        help="existing validation session directory",
    )
    return parser.parse_args()


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def main() -> int:
    args = parse_args()
    session = args.session.resolve()
    if not session.is_dir():
        print(f"session directory does not exist: {session}", file=sys.stderr)
        return 2

    manifest = session / "manifest.sha256"
    temporary = session / ".manifest.sha256.tmp"
    files = []
    for path in session.rglob("*"):
        if path in {manifest, temporary}:
            continue
        if path.is_symlink():
            print(f"symbolic links are not allowed in evidence: {path}", file=sys.stderr)
            return 2
        if path.is_file():
            files.append(path)

    lines = []
    for path in sorted(files, key=lambda item: item.relative_to(session).as_posix()):
        relative = path.relative_to(session).as_posix()
        lines.append(f"{digest(path)}  {relative}\n")

    temporary.write_text("".join(lines), encoding="utf-8")
    os.replace(temporary, manifest)
    print(f"manifest written: {manifest} ({len(files)} files)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
