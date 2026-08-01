#!/usr/bin/env python3
"""Verify that resolved Heltec build identity is embedded in a firmware image."""

from __future__ import annotations

import argparse
from pathlib import Path

from build_metadata import resolve_build_metadata


def require_c_string(image: bytes, value: str, label: str) -> None:
    token = value.encode("ascii") + b"\x00"
    if token not in image:
        raise ValueError(f"{label} {value!r} is not embedded as a C string")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    parser.add_argument(
        "--project-dir",
        type=Path,
        default=Path(__file__).resolve().parents[2],
    )
    args = parser.parse_args()

    metadata = resolve_build_metadata(args.project_dir.resolve())
    image = args.binary.read_bytes()
    require_c_string(image, metadata.version, "firmware version")
    require_c_string(image, metadata.commit, "firmware commit")
    if b"Homebrew\x00" in image:
        raise ValueError("legacy Homebrew commit marker is still embedded")
    print(
        "PASS firmware metadata "
        f"version={metadata.version} commit={metadata.commit} bytes={len(image)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
