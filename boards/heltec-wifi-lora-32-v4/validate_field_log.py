#!/usr/bin/env python3
"""Validate reset-resistant Heltec field-log NDJSON segments.

An interrupted final line is allowed only at the tail of a segment. Every
newline-terminated record must be valid JSON with the expected session and
segment identifiers.
"""

from __future__ import annotations

import argparse
import json
import re
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable


FILE_NAME = re.compile(r"session-(?P<session>\d{6})-(?P<segment>\d{3})\.ndjson$")
BLE_ADDRESS = re.compile(r"(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}$")
KNOWN_TYPES = {
    "session_start",
    "session_resume",
    "tail_recovery",
    "session_suspend",
    "session_interrupted",
    "session_stop",
    "gps",
    "ble",
    "wifi",
}


class ValidationFailure(RuntimeError):
    pass


@dataclass
class SegmentResult:
    path: Path
    session: int
    segment: int
    records: list[dict[str, Any]]
    interrupted_tail_bytes: int


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValidationFailure(message)


def validate_record(record: Any, path: Path, line_number: int, session: int, segment: int) -> None:
    label = f"{path.name}:{line_number}"
    require(isinstance(record, dict), f"{label}: record is not a JSON object")
    require(record.get("formatVersion") == 1, f"{label}: unsupported formatVersion")
    require(record.get("type") in KNOWN_TYPES, f"{label}: unknown record type")
    require(record.get("sessionId") == session, f"{label}: sessionId does not match file name")
    require(record.get("segment") == segment, f"{label}: segment does not match file name")
    require(isinstance(record.get("bootCount"), int), f"{label}: bootCount is missing")
    require(isinstance(record.get("uptimeMs"), int), f"{label}: uptimeMs is missing")

    if record["type"] == "gps":
        latitude = record.get("latitude")
        longitude = record.get("longitude")
        require(
            isinstance(latitude, (int, float)) and not isinstance(latitude, bool) and -90 <= latitude <= 90,
            f"{label}: invalid GPS latitude",
        )
        require(
            isinstance(longitude, (int, float)) and not isinstance(longitude, bool) and -180 <= longitude <= 180,
            f"{label}: invalid GPS longitude",
        )
    elif record["type"] == "ble":
        address = record.get("address")
        require(isinstance(address, str) and BLE_ADDRESS.fullmatch(address) is not None, f"{label}: invalid BLE address")
        rssi = record.get("rssiDbm")
        require(
            isinstance(rssi, int) and -127 <= rssi <= 20,
            f"{label}: invalid BLE RSSI",
        )
        location = record.get("location")
        if location is not None:
            require(isinstance(location, dict), f"{label}: BLE location is not an object")
            require(-90 <= float(location.get("latitude")) <= 90, f"{label}: invalid associated latitude")
            require(-180 <= float(location.get("longitude")) <= 180, f"{label}: invalid associated longitude")
    elif record["type"] == "wifi":
        require(record.get("source") == "android", f"{label}: invalid Wi-Fi source")
        bssid = record.get("bssid")
        require(isinstance(bssid, str) and BLE_ADDRESS.fullmatch(bssid) is not None, f"{label}: invalid Wi-Fi BSSID")
        ssid = record.get("ssid")
        require(
            ssid is None or (isinstance(ssid, str) and len(ssid) <= 64),
            f"{label}: invalid Wi-Fi SSID",
        )
        rssi = record.get("rssiDbm")
        require(isinstance(rssi, int) and -127 <= rssi <= 20, f"{label}: invalid Wi-Fi RSSI")
        frequency = record.get("frequencyMhz")
        require(isinstance(frequency, int) and 2000 <= frequency <= 7200, f"{label}: invalid Wi-Fi frequency")
        location = record.get("location")
        if location is not None:
            require(isinstance(location, dict), f"{label}: Wi-Fi location is not an object")
            require(-90 <= float(location.get("latitude")) <= 90, f"{label}: invalid associated latitude")
            require(-180 <= float(location.get("longitude")) <= 180, f"{label}: invalid associated longitude")


def validate_segment(path: Path) -> SegmentResult:
    match = FILE_NAME.fullmatch(path.name)
    require(match is not None, f"unexpected field-log file name: {path.name}")
    session = int(match.group("session"))
    segment = int(match.group("segment"))
    raw = path.read_bytes()
    complete_tail = not raw or raw.endswith(b"\n")
    parts = raw.split(b"\n")
    complete_lines = parts[:-1]
    interrupted_tail = b"" if complete_tail else parts[-1]
    records: list[dict[str, Any]] = []

    for line_number, encoded in enumerate(complete_lines, start=1):
        if not encoded:
            continue
        try:
            record = json.loads(encoded)
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            raise ValidationFailure(f"{path.name}:{line_number}: invalid complete NDJSON record: {error}") from error
        validate_record(record, path, line_number, session, segment)
        records.append(record)

    require(records or interrupted_tail, f"{path.name}: empty segment")
    return SegmentResult(path, session, segment, records, len(interrupted_tail))


def validate_files(paths: Iterable[Path]) -> list[SegmentResult]:
    results = sorted((validate_segment(path) for path in paths), key=lambda item: (item.session, item.segment))
    require(bool(results), "no field-log segments found")
    seen: set[tuple[int, int]] = set()
    by_session: dict[int, list[SegmentResult]] = {}
    for result in results:
        key = (result.session, result.segment)
        require(key not in seen, f"duplicate session/segment: {key}")
        seen.add(key)
        by_session.setdefault(result.session, []).append(result)

    for session, segments in by_session.items():
        expected = list(range(segments[0].segment, segments[-1].segment + 1))
        actual = [segment.segment for segment in segments]
        require(actual == expected, f"session {session}: missing segment between {actual}")
        for current, following in zip(segments, segments[1:]):
            if current.interrupted_tail_bytes:
                require(
                    any(record.get("type") == "tail_recovery" for record in following.records),
                    f"{following.path.name}: missing tail_recovery after interrupted segment",
                )

    return results


def run_self_test() -> None:
    with tempfile.TemporaryDirectory(prefix="heltec-field-log-") as directory_name:
        directory = Path(directory_name)
        common = {"formatVersion": 1, "sessionId": 7, "bootCount": 1}
        first = directory / "session-000007-000.ndjson"
        records = [
            common | {"type": "session_start", "segment": 0, "uptimeMs": 100},
            common
            | {
                "type": "gps",
                "segment": 0,
                "uptimeMs": 200,
                "latitude": 41.88,
                "longitude": -87.63,
            },
        ]
        first.write_bytes(("\n".join(json.dumps(record) for record in records) + "\n{\"formatVersion\":1").encode())
        second = directory / "session-000007-001.ndjson"
        second_records = [
            common
            | {
                "type": "tail_recovery",
                "segment": 1,
                "bootCount": 2,
                "uptimeMs": 50,
                "previousSegment": 0,
            },
            common
            | {
                "type": "ble",
                "segment": 1,
                "bootCount": 2,
                "uptimeMs": 100,
                "address": "00:11:22:33:44:55",
                "rssiDbm": -60,
            },
            common
            | {
                "type": "wifi",
                "segment": 1,
                "bootCount": 2,
                "uptimeMs": 150,
                "source": "android",
                "bssid": "02:00:00:00:00:01",
                "ssid": "FIELD-TEST",
                "rssiDbm": -48,
                "frequencyMhz": 2437,
            },
            common | {"type": "session_stop", "segment": 1, "bootCount": 2, "uptimeMs": 200},
        ]
        second.write_text("\n".join(json.dumps(record) for record in second_records) + "\n")
        results = validate_files(directory.glob("*.ndjson"))
        require(sum(len(result.records) for result in results) == 6, "self-test record count mismatch")
        require(results[0].interrupted_tail_bytes > 0, "self-test did not detect interrupted tail")
    print("PASS field-log validator self-test")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="*", type=Path, help="NDJSON segment files or directories")
    parser.add_argument("--self-test", action="store_true", help="run the built-in reset-recovery fixture")
    args = parser.parse_args()
    if args.self_test:
        run_self_test()
        return 0
    require(bool(args.paths), "provide one or more field-log files or directories")
    files: list[Path] = []
    for path in args.paths:
        files.extend(path.glob("session-*.ndjson") if path.is_dir() else [path])
    results = validate_files(files)
    record_count = sum(len(result.records) for result in results)
    interrupted = sum(result.interrupted_tail_bytes > 0 for result in results)
    sessions = len({result.session for result in results})
    print(
        f"PASS {record_count} complete records across {len(results)} segments / "
        f"{sessions} sessions; interrupted tails={interrupted}"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ValidationFailure as error:
        print(f"FAIL: {error}")
        raise SystemExit(1) from error
