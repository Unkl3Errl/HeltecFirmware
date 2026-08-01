#!/usr/bin/env python3
"""Passive WebUI smoke test for the Heltec WiFi LoRa 32 V4 port.

This script never supplies a LoRa payload or the literal TRANSMIT confirmation.
It is safe to use for regression checks that must not emit an RF packet.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from datetime import datetime, timezone
from http.cookiejar import CookieJar
from ipaddress import IPv4Address, ip_address
from time import monotonic, sleep
from typing import Any
from urllib.error import HTTPError, URLError
from urllib.parse import urlencode
from urllib.request import HTTPCookieProcessor, Request, build_opener


class SmokeTestFailure(RuntimeError):
    pass


def request(
    opener: Any,
    base_url: str,
    method: str,
    path: str,
    fields: dict[str, str] | None = None,
    timeout: float = 5.0,
) -> tuple[int, str]:
    data = None
    if method == "POST":
        data = urlencode(fields or {}).encode("ascii")
    req = Request(base_url + path, data=data, method=method)
    if data is not None:
        req.add_header("Content-Type", "application/x-www-form-urlencoded")
    try:
        with opener.open(req, timeout=timeout) as response:
            return response.status, response.read().decode("utf-8", errors="replace")
    except HTTPError as error:
        return error.code, error.read().decode("utf-8", errors="replace")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SmokeTestFailure(message)


def parse_json(body: str, label: str) -> dict[str, Any]:
    try:
        value = json.loads(body)
    except json.JSONDecodeError as error:
        raise SmokeTestFailure(f"{label} did not return JSON: {error}") from error
    require(isinstance(value, dict), f"{label} returned a non-object JSON value")
    return value


def nested_keys(value: Any) -> set[str]:
    if isinstance(value, dict):
        keys = {str(key).lower() for key in value}
        for child in value.values():
            keys.update(nested_keys(child))
        return keys
    if isinstance(value, list):
        keys: set[str] = set()
        for child in value:
            keys.update(nested_keys(child))
        return keys
    return set()


def run_soak(
    args: argparse.Namespace,
    authenticated: Any,
    base_url: str,
    board_status: dict[str, Any],
    gps_track: dict[str, Any],
    lora_status: dict[str, Any],
    lora_history: dict[str, Any],
    field_log: dict[str, Any],
) -> None:
    if args.soak_seconds <= 0:
        return

    expected_firmware = board_status["firmware"]
    expected_network = {
        key: board_status["network"].get(key)
        for key in ("mode", "apActive", "ssid", "ip", "mac", "channel")
    }
    expected_gps_state = board_status["gps"].get("monitorState")
    expected_gps_power = board_status["gps"].get("powered")
    expected_gps_sequences = [point.get("sequence") for point in gps_track["points"]]
    expected_lora_sequences = [packet.get("sequence") for packet in lora_history["packets"]]
    expected_listening = bool(lora_status.get("listening"))
    expected_received = int(lora_status.get("packetsReceived", 0))
    expected_transmitted = int(lora_status.get("transmittedPackets", 0))
    expected_heap_total = int(board_status["system"]["heap"]["totalBytes"])
    expected_psram_total = int(board_status["system"]["psram"]["totalBytes"])
    expected_field_state = {
        key: field_log.get(key) for key in ("active", "autoResume", "sessionId", "segment")
    }
    previous_uptime = int(board_status["system"]["uptimeMs"])
    free_heap_samples: list[int] = []
    free_psram_samples: list[int] = []
    request_cycle_seconds: list[float] = []
    samples = 0
    started = monotonic()
    deadline = started + args.soak_seconds
    next_progress = started + min(30.0, args.soak_seconds)

    print(
        f"START passive WebUI soak for {args.soak_seconds:g}s "
        f"at {args.soak_interval:g}s intervals"
    )
    while samples == 0 or monotonic() < deadline:
        cycle_started = monotonic()

        status, body = request(
            authenticated, base_url, "GET", "/api/heltec/status", timeout=args.timeout
        )
        require(status == 200, f"soak hardware status returned HTTP {status}")
        current_board = parse_json(body, "soak hardware status")
        require(current_board.get("board") == board_status["board"], "board identity changed during soak")
        require(current_board.get("firmware") == expected_firmware, "firmware identity changed during soak")
        require(
            all(current_board.get("network", {}).get(key) == value for key, value in expected_network.items()),
            "access-point identity changed during soak",
        )
        require(
            1 <= int(current_board["network"].get("connectedClients", 0)) <= 4,
            "access-point lost all clients during soak",
        )

        current_system = current_board.get("system", {})
        current_uptime = int(current_system.get("uptimeMs", -1))
        require(current_uptime > previous_uptime, "uptime did not increase; board may have rebooted")
        previous_uptime = current_uptime
        current_heap = current_system.get("heap", {})
        current_heap_free = int(current_heap.get("freeBytes", 0))
        require(
            int(current_heap.get("totalBytes", 0)) == expected_heap_total,
            "heap capacity changed during soak",
        )
        require(
            current_heap_free >= args.minimum_free_heap,
            f"free heap fell below {args.minimum_free_heap} bytes during soak",
        )
        current_psram = current_system.get("psram", {})
        current_psram_free = int(current_psram.get("freeBytes", 0))
        require(
            int(current_psram.get("totalBytes", 0)) == expected_psram_total,
            "PSRAM capacity changed during soak",
        )
        require(
            current_psram_free >= args.minimum_free_psram,
            f"free PSRAM fell below {args.minimum_free_psram} bytes during soak",
        )
        require(
            current_board.get("gps", {}).get("monitorState") == expected_gps_state
            and current_board.get("gps", {}).get("powered") == expected_gps_power,
            "GPS state changed during soak",
        )

        status, body = request(
            authenticated, base_url, "GET", "/api/heltec/gps/history", timeout=args.timeout
        )
        require(status == 200, f"soak GPS history returned HTTP {status}")
        current_track = parse_json(body, "soak GPS history")
        require(
            [point.get("sequence") for point in current_track.get("points", [])]
            == expected_gps_sequences,
            "GPS history changed while monitoring was inactive",
        )

        status, body = request(
            authenticated, base_url, "GET", "/api/heltec/lora", timeout=args.timeout
        )
        require(status == 200, f"soak LoRa status returned HTTP {status}")
        current_lora = parse_json(body, "soak LoRa status")
        require(
            bool(current_lora.get("listening")) == expected_listening,
            "LoRa receiver state changed during soak",
        )
        require(
            int(current_lora.get("packetsReceived", 0)) == expected_received,
            "LoRa receive counter changed during soak",
        )
        require(
            int(current_lora.get("transmittedPackets", 0)) == expected_transmitted,
            "LoRa transmit counter changed during soak",
        )

        status, body = request(
            authenticated, base_url, "GET", "/api/heltec/lora/history", timeout=args.timeout
        )
        require(status == 200, f"soak LoRa history returned HTTP {status}")
        current_history = parse_json(body, "soak LoRa history")
        require(
            [packet.get("sequence") for packet in current_history.get("packets", [])]
            == expected_lora_sequences,
            "LoRa history changed during soak",
        )

        status, body = request(
            authenticated, base_url, "GET", "/api/heltec/fieldlog", timeout=args.timeout
        )
        require(status == 200, f"soak field-log status returned HTTP {status}")
        current_field_log = parse_json(body, "soak field-log status")
        require(
            all(current_field_log.get(key) == value for key, value in expected_field_state.items()),
            "field-log session state changed during passive soak",
        )

        free_heap_samples.append(current_heap_free)
        free_psram_samples.append(current_psram_free)
        request_cycle_seconds.append(monotonic() - cycle_started)
        samples += 1
        now = monotonic()
        if now >= next_progress and now < deadline:
            print(
                f"SOAK {min(now - started, args.soak_seconds):.0f}/{args.soak_seconds:g}s: "
                f"samples={samples}, heap_min={min(free_heap_samples)}, "
                f"psram_min={min(free_psram_samples)}"
            )
            next_progress += 30.0
        if now < deadline:
            sleep(min(args.soak_interval, deadline - now))

    require(
        free_heap_samples[-1] >= free_heap_samples[0] - args.maximum_final_heap_drop,
        "final free heap degraded beyond the allowed soak tolerance",
    )
    require(
        free_psram_samples[-1] >= free_psram_samples[0] - args.maximum_final_psram_drop,
        "final free PSRAM degraded beyond the allowed soak tolerance",
    )
    print(
        f"PASS passive WebUI soak: {samples} samples, "
        f"heap={min(free_heap_samples)}..{max(free_heap_samples)} bytes, "
        f"PSRAM={min(free_psram_samples)}..{max(free_psram_samples)} bytes, "
        f"slowest cycle={max(request_cycle_seconds):.3f}s"
    )


def run(args: argparse.Namespace) -> None:
    base_url = args.url.rstrip("/")

    anonymous = build_opener()
    status, _ = request(anonymous, base_url, "GET", "/api/heltec/status", timeout=args.timeout)
    require(status == 401, f"unauthenticated hardware status returned HTTP {status}, expected 401")
    for path in (
        "/api/heltec/fieldlog/phone-gps",
        "/api/heltec/fieldlog",
        "/api/heltec/fieldlog/files",
    ):
        status, _ = request(anonymous, base_url, "GET", path, timeout=args.timeout)
        require(status == 401, f"unauthenticated {path} returned HTTP {status}, expected 401")
    print("PASS unauthenticated hardware and field-log data are rejected")

    cookies = CookieJar()
    authenticated = build_opener(HTTPCookieProcessor(cookies))
    status, _ = request(
        authenticated,
        base_url,
        "POST",
        "/login",
        {"username": args.username, "password": args.password},
        timeout=args.timeout,
    )
    require(status == 200, f"login flow ended with HTTP {status}, expected 200 after redirect")
    require(
        any(cookie.name == "BRUCESESSION" and cookie.value != "0" for cookie in cookies),
        "login did not issue a BRUCESESSION cookie",
    )
    print("PASS authenticated WebUI session established")

    try:
        status, body = request(
            authenticated, base_url, "GET", "/api/heltec/status", timeout=args.timeout
        )
        require(status == 200, f"hardware status returned HTTP {status}")
        board_status = parse_json(body, "hardware status")
        require(
            board_status.get("board") == "Heltec WiFi LoRa 32 V4",
            f"unexpected board identity: {board_status.get('board')!r}",
        )
        require("sx1262" in board_status and "gps" in board_status, "hardware status is incomplete")
        firmware = board_status.get("firmware")
        require(isinstance(firmware, dict), "hardware status did not include firmware metadata")
        require(
            isinstance(firmware.get("version"), str) and bool(firmware["version"]),
            "firmware version is missing",
        )
        require(
            isinstance(firmware.get("commit"), str) and bool(firmware["commit"]),
            "firmware commit is missing",
        )
        system = board_status.get("system")
        require(isinstance(system, dict), "hardware status did not include system health")
        require(
            isinstance(system.get("uptimeMs"), int) and system["uptimeMs"] >= 0,
            "system uptime is invalid",
        )
        heap = system.get("heap")
        require(isinstance(heap, dict), "system health did not include heap metrics")
        heap_total = heap.get("totalBytes")
        heap_free = heap.get("freeBytes")
        heap_minimum = heap.get("minimumFreeBytes")
        heap_maximum_allocation = heap.get("maximumAllocationBytes")
        require(isinstance(heap_total, int) and heap_total > 0, "heap total is invalid")
        require(
            isinstance(heap_free, int) and 0 < heap_free <= heap_total,
            "free heap is invalid",
        )
        require(
            isinstance(heap_minimum, int) and 0 < heap_minimum <= heap_total,
            "minimum free heap is invalid",
        )
        require(
            isinstance(heap_maximum_allocation, int) and 0 < heap_maximum_allocation <= heap_free,
            "maximum heap allocation is invalid",
        )
        psram = system.get("psram")
        require(isinstance(psram, dict), "system health did not include PSRAM metrics")
        require(isinstance(psram.get("present"), bool), "PSRAM presence flag is invalid")
        psram_total = psram.get("totalBytes")
        psram_free = psram.get("freeBytes")
        require(isinstance(psram_total, int) and psram_total >= 0, "PSRAM total is invalid")
        require(
            isinstance(psram_free, int) and 0 <= psram_free <= psram_total,
            "free PSRAM is invalid",
        )
        if psram["present"]:
            require(psram_total > 0 and psram_free > 0, "detected PSRAM did not report capacity")
        network = board_status.get("network")
        require(isinstance(network, dict), "hardware status did not include network telemetry")
        require(network.get("apActive") is True, "WebUI access point is not reported active")
        require(network.get("mode") in {"AP", "AP+STA"}, "WebUI reported an invalid Wi-Fi mode")
        require(
            isinstance(network.get("ssid"), str) and 1 <= len(network["ssid"]) <= 32,
            "access-point SSID is invalid",
        )
        try:
            network_ip = ip_address(network.get("ip", ""))
        except ValueError as error:
            raise SmokeTestFailure(f"access-point IP is invalid: {network.get('ip')!r}") from error
        require(isinstance(network_ip, IPv4Address), "access-point IP is not IPv4")
        require(
            isinstance(network.get("mac"), str)
            and re.fullmatch(r"(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}", network["mac"]) is not None,
            "access-point MAC is invalid",
        )
        require(
            isinstance(network.get("channel"), int) and 1 <= network["channel"] <= 14,
            "access-point channel is invalid",
        )
        require(
            isinstance(network.get("connectedClients"), int)
            and 1 <= network["connectedClients"] <= 4,
            "access-point client count is invalid",
        )
        print(
            "PASS board identity, firmware metadata, and runtime health are available "
            f"(heap={heap_free}/{heap_total}, psram={psram_free}/{psram_total} bytes)"
        )
        print(
            "PASS WebUI access-point telemetry is available "
            f"({network['ssid']}, {network_ip}, channel {network['channel']}, "
            f"clients={network['connectedClients']})"
        )

        status, body = request(
            authenticated, base_url, "GET", "/api/heltec/gps/history", timeout=args.timeout
        )
        require(status == 200, f"GPS track returned HTTP {status}")
        gps_track = parse_json(body, "GPS track")
        require(
            isinstance(gps_track.get("points"), list) and int(gps_track.get("capacity", 0)) > 0,
            "GPS track route returned an invalid payload",
        )
        require(
            int(gps_track.get("count", -1)) == len(gps_track["points"]),
            "GPS track count does not match its point array",
        )
        require(
            int(gps_track.get("minimumIntervalMs", 0)) > 0,
            "GPS track did not report a positive sampling interval",
        )
        previous_sequence = 0
        previous_capture_ms = -1
        previous_utc: datetime | None = None
        for point in gps_track["points"]:
            require(isinstance(point, dict), "GPS track contains a non-object point")
            sequence = point.get("sequence")
            captured_at_ms = point.get("capturedAtMs")
            latitude = point.get("latitude")
            longitude = point.get("longitude")
            require(
                isinstance(sequence, int) and sequence > previous_sequence,
                "GPS track sequence is not strictly increasing",
            )
            require(
                isinstance(captured_at_ms, int) and captured_at_ms >= previous_capture_ms,
                "GPS track capture time is not monotonic",
            )
            require(
                isinstance(latitude, (int, float))
                and not isinstance(latitude, bool)
                and -90 <= latitude <= 90,
                "GPS track contains an invalid latitude",
            )
            require(
                isinstance(longitude, (int, float))
                and not isinstance(longitude, bool)
                and -180 <= longitude <= 180,
                "GPS track contains an invalid longitude",
            )
            utc = point.get("utc")
            if utc is not None:
                require(isinstance(utc, str) and utc.endswith("Z"), "GPS track UTC is invalid")
                try:
                    parsed_utc = datetime.fromisoformat(utc.removesuffix("Z") + "+00:00")
                except ValueError as error:
                    raise SmokeTestFailure(f"GPS track UTC is invalid: {utc!r}") from error
                require(parsed_utc.tzinfo == timezone.utc, "GPS track UTC is not UTC")
                require(
                    previous_utc is None or parsed_utc >= previous_utc,
                    "GPS track UTC is not monotonic",
                )
                previous_utc = parsed_utc
            previous_sequence = sequence
            previous_capture_ms = captured_at_ms
        for _ in range(args.history_polls - 1):
            status, body = request(
                authenticated, base_url, "GET", "/api/heltec/gps/history", timeout=args.timeout
            )
            require(status == 200, f"repeated GPS track poll returned HTTP {status}")
            repeated_track = parse_json(body, "repeated GPS track poll")
            require(
                isinstance(repeated_track.get("points"), list),
                "repeated GPS track poll returned an invalid payload",
            )
        print(
            "PASS bounded GPS fix track remained stable across "
            f"{args.history_polls} polls ({gps_track['count']} of {gps_track['capacity']} points)"
        )

        status, body = request(
            authenticated, base_url, "GET", "/api/heltec/lora", timeout=args.timeout
        )
        require(status == 200, f"LoRa status returned HTTP {status}")
        initial_lora = parse_json(body, "LoRa status")
        require(initial_lora.get("radio") == "SX1262", "WebUI did not report the SX1262")
        require(initial_lora.get("transmitAvailable") is True, "constrained transmitter is unavailable")
        initial_transmitted = int(initial_lora.get("transmittedPackets", 0))
        initial_listening = bool(initial_lora.get("listening"))
        print(
            "PASS LoRa status is available "
            f"(listening={str(initial_listening).lower()}, transmitted={initial_transmitted})"
        )

        status, body = request(
            authenticated, base_url, "GET", "/api/heltec/lora/history", timeout=args.timeout
        )
        require(status == 200, f"LoRa history returned HTTP {status}")
        history = parse_json(body, "LoRa history")
        require(
            isinstance(history.get("packets"), list) and int(history.get("capacity", 0)) > 0,
            "LoRa history route returned an invalid payload",
        )
        require(
            int(history.get("count", -1)) == len(history["packets"]),
            "LoRa history count does not match its packet array",
        )
        previous_sequence = 0
        previous_received_at_ms = -1
        history_sequences: list[int] = []
        for packet in history["packets"]:
            require(isinstance(packet, dict), "LoRa history contains a non-object packet")
            sequence = packet.get("sequence")
            received_at_ms = packet.get("receivedAtMs")
            age_ms = packet.get("ageMs")
            message = packet.get("message")
            rssi_dbm = packet.get("rssiDbm")
            snr_db = packet.get("snrDb")
            require(
                isinstance(sequence, int) and sequence > previous_sequence,
                "LoRa history sequence is not strictly increasing",
            )
            require(
                isinstance(received_at_ms, int) and received_at_ms >= previous_received_at_ms,
                "LoRa history receive time is not monotonic",
            )
            require(isinstance(age_ms, int) and age_ms >= 0, "LoRa history age is invalid")
            require(
                isinstance(message, str)
                and len(message) <= 256
                and all(32 <= ord(character) <= 126 for character in message),
                "LoRa history payload preview is invalid",
            )
            require(
                isinstance(rssi_dbm, (int, float))
                and not isinstance(rssi_dbm, bool)
                and -200 <= rssi_dbm <= 50,
                "LoRa history RSSI is invalid",
            )
            require(
                isinstance(snr_db, (int, float))
                and not isinstance(snr_db, bool)
                and -30 <= snr_db <= 30,
                "LoRa history SNR is invalid",
            )
            history_sequences.append(sequence)
            previous_sequence = sequence
            previous_received_at_ms = received_at_ms
        print(
            "PASS bounded LoRa receive history is available "
            f"({history['count']} of {history['capacity']} entries)"
        )

        status, body = request(
            authenticated, base_url, "GET", "/api/heltec/fieldlog", timeout=args.timeout
        )
        require(status == 200, f"field-log status returned HTTP {status}")
        field_log = parse_json(body, "field-log status")
        require(field_log.get("formatVersion") == 1, "field-log format version is invalid")
        require(field_log.get("initialized") is True, "field logger is not initialized")
        require(isinstance(field_log.get("active"), bool), "field-log active state is invalid")
        require(isinstance(field_log.get("autoResume"), bool), "field-log auto-resume state is invalid")
        require(isinstance(field_log.get("sessionId"), int), "field-log session ID is invalid")
        field_gps = field_log.get("gps")
        field_ble = field_log.get("ble")
        field_storage = field_log.get("storage")
        require(isinstance(field_gps, dict), "field-log GPS status is missing")
        require(isinstance(field_ble, dict), "field-log BLE status is missing")
        require(isinstance(field_storage, dict), "field-log storage status is missing")
        require(
            isinstance(field_gps.get("fixes"), int) and field_gps["fixes"] >= 0,
            "field-log GPS counter is invalid",
        )
        require(
            isinstance(field_gps.get("phoneFixes"), int)
            and 0 <= field_gps["phoneFixes"] <= field_gps["fixes"],
            "field-log phone GPS counter is invalid",
        )
        require(
            isinstance(field_ble.get("observations"), int) and field_ble["observations"] >= 0,
            "field-log BLE observation counter is invalid",
        )
        require(
            isinstance(field_ble.get("uniqueDevices"), int)
            and 0 <= field_ble["uniqueDevices"] <= int(field_ble.get("uniqueCapacity", -1)),
            "field-log BLE unique-device counter is invalid",
        )
        total_storage = int(field_storage.get("totalBytes", 0))
        used_storage = int(field_storage.get("usedBytes", -1))
        require(
            total_storage > 0 and 0 <= used_storage <= total_storage,
            "field-log storage capacity is invalid",
        )

        status, body = request(
            authenticated, base_url, "GET", "/api/heltec/fieldlog/files", timeout=args.timeout
        )
        require(status == 200, f"field-log file list returned HTTP {status}")
        field_log_files = parse_json(body, "field-log file list")
        files = field_log_files.get("files")
        require(isinstance(files, list), "field-log file list is invalid")
        require(
            int(field_log_files.get("count", -1)) >= len(files),
            "field-log file count is invalid",
        )
        for item in files:
            require(isinstance(item, dict), "field-log file list contains a non-object")
            require(
                isinstance(item.get("name"), str)
                and re.fullmatch(r"session-\d{6}-\d{3}\.ndjson", item["name"]) is not None,
                "field-log file name is unsafe",
            )
            require(
                isinstance(item.get("sizeBytes"), int) and item["sizeBytes"] >= 0,
                "field-log file size is invalid",
            )
            require(isinstance(item.get("tailComplete"), bool), "field-log tail state is invalid")
        if field_log["active"]:
            require(
                any(
                    item.get("name") == field_storage.get("fileName") and item.get("active")
                    for item in files
                ),
                "active field-log segment is absent from the file list",
            )
        print(
            "PASS reset-resistant field-log status and authenticated file list are available "
            f"(active={str(field_log['active']).lower()}, files={len(files)})"
        )

        status, _ = request(
            authenticated,
            base_url,
            "GET",
            "/api/heltec/fieldlog/download?name=../config.conf",
            timeout=args.timeout,
        )
        require(status == 404, f"field-log path traversal returned HTTP {status}, expected 404")
        print("PASS field-log download rejects path traversal")

        diagnostic_snapshot = {
            "formatVersion": 1,
            "exportedAt": datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
            "hardware": board_status,
            "gpsTrack": gps_track,
            "loraStatus": initial_lora,
            "loraHistory": history,
            "fieldLog": field_log,
            "fieldLogFiles": field_log_files,
        }
        diagnostic_round_trip = json.loads(json.dumps(diagnostic_snapshot))
        require(
            set(diagnostic_round_trip)
            == {
                "formatVersion",
                "exportedAt",
                "hardware",
                "gpsTrack",
                "loraStatus",
                "loraHistory",
                "fieldLog",
                "fieldLogFiles",
            },
            "diagnostic snapshot schema is incomplete",
        )
        forbidden_keys = {"username", "password", "session", "brucesession", "cookie", "authorization"}
        require(
            nested_keys(diagnostic_round_trip).isdisjoint(forbidden_keys),
            "diagnostic snapshot contains an authentication-related key",
        )
        print("PASS combined diagnostic snapshot contract excludes authentication data")
        for _ in range(args.history_polls - 1):
            status, body = request(
                authenticated, base_url, "GET", "/api/heltec/lora/history", timeout=args.timeout
            )
            require(status == 200, f"repeated LoRa history poll returned HTTP {status}")
            repeated_history = parse_json(body, "repeated LoRa history poll")
            require(
                isinstance(repeated_history.get("packets"), list),
                "repeated LoRa history poll returned an invalid payload",
            )
            require(
                int(repeated_history.get("count", -1)) == len(repeated_history["packets"]),
                "repeated LoRa history count does not match its packet array",
            )
            require(
                [packet.get("sequence") for packet in repeated_history["packets"]]
                == history_sequences,
                "LoRa history changed during passive polling",
            )
        print(f"PASS LoRa history remained stable across {args.history_polls} consecutive polls")

        status, body = request(
            authenticated,
            base_url,
            "POST",
            "/api/heltec/lora/transmit",
            timeout=args.timeout,
        )
        require(status == 400, f"empty transmit request returned HTTP {status}, expected 400")
        error = parse_json(body, "empty transmit request").get("error")
        require(
            error == "payload and confirmation are required",
            f"transmit route was not selected first: {error!r}",
        )
        print("PASS specific transmit route precedes the generic LoRa route")

        status, body = request(
            authenticated, base_url, "POST", "/api/heltec/lora", timeout=args.timeout
        )
        require(status == 400, f"empty LoRa control request returned HTTP {status}, expected 400")
        error = parse_json(body, "empty LoRa control request").get("error")
        require(error == "missing action", f"generic LoRa route returned the wrong error: {error!r}")
        print("PASS generic LoRa route remains independently reachable")

        status, body = request(
            authenticated, base_url, "POST", "/api/heltec/fieldlog", timeout=args.timeout
        )
        require(status == 400, f"empty field-log control returned HTTP {status}, expected 400")
        error = parse_json(body, "empty field-log control").get("error")
        require(error == "missing action", f"field-log control returned the wrong error: {error!r}")
        print("PASS field-log control requires an explicit action")

        status, body = request(
            authenticated,
            base_url,
            "POST",
            "/api/heltec/fieldlog/phone-gps",
            {"latitude": "91", "longitude": "0"},
            timeout=args.timeout,
        )
        require(status == 400, f"invalid phone GPS fix returned HTTP {status}, expected 400")
        error = parse_json(body, "invalid phone GPS response").get("error")
        require(error == "invalid coordinates", f"phone GPS route returned the wrong error: {error!r}")
        print("PASS phone-assisted GPS rejects invalid coordinates without changing logger state")

        status, body = request(
            authenticated, base_url, "POST", "/reboot", timeout=args.timeout
        )
        require(status == 400, f"unconfirmed reboot request returned HTTP {status}, expected 400")
        error = parse_json(body, "unconfirmed reboot response").get("error")
        require(
            error == "restart action and confirmation are required",
            f"unconfirmed reboot returned the wrong error: {error!r}",
        )
        print("PASS reboot route rejects requests without explicit confirmation")

        status, body = request(
            authenticated,
            base_url,
            "POST",
            "/api/heltec/lora",
            {"action": "start", "frequencyMHz": "0"},
            timeout=args.timeout,
        )
        require(status == 400, f"out-of-band frequency returned HTTP {status}, expected 400")
        rejected_lora = parse_json(body, "out-of-band frequency response")
        require(
            bool(rejected_lora.get("listening")) == initial_listening,
            "rejected frequency unexpectedly changed receiver state",
        )
        print("PASS out-of-band frequency is rejected without changing receiver state")

        status, body = request(
            authenticated, base_url, "GET", "/api/heltec/lora", timeout=args.timeout
        )
        require(status == 200, f"final LoRa status returned HTTP {status}")
        final_lora = parse_json(body, "final LoRa status")
        final_transmitted = int(final_lora.get("transmittedPackets", 0))
        require(
            final_transmitted == initial_transmitted,
            f"transmit counter changed from {initial_transmitted} to {final_transmitted}",
        )
        print(f"PASS transmit counter remained unchanged at {final_transmitted}")
        status, body = request(
            authenticated, base_url, "GET", "/api/heltec/fieldlog", timeout=args.timeout
        )
        require(status == 200, f"final field-log status returned HTTP {status}")
        final_field_log = parse_json(body, "final field-log status")
        require(
            all(
                final_field_log.get(key) == field_log.get(key)
                for key in ("active", "autoResume", "sessionId", "segment")
            ),
            "passive smoke test changed field-log session state",
        )
        print("PASS passive checks left field-log session state unchanged")
        run_soak(
            args,
            authenticated,
            base_url,
            board_status,
            gps_track,
            final_lora,
            history,
            final_field_log,
        )
    finally:
        request(authenticated, base_url, "GET", "/logout", timeout=args.timeout)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", default="http://172.0.0.1", help="Bruce WebUI base URL")
    parser.add_argument("--username", default="admin", help="WebUI username")
    parser.add_argument("--password", default="bruce", help="WebUI password")
    parser.add_argument("--timeout", type=float, default=5.0, help="HTTP timeout in seconds")
    parser.add_argument(
        "--history-polls",
        type=int,
        default=5,
        help="number of consecutive passive history requests",
    )
    parser.add_argument(
        "--soak-seconds",
        type=float,
        default=0,
        help="sustained passive polling duration; zero disables the soak",
    )
    parser.add_argument(
        "--soak-interval",
        type=float,
        default=2,
        help="delay between passive soak samples",
    )
    parser.add_argument("--minimum-free-heap", type=int, default=65536)
    parser.add_argument("--minimum-free-psram", type=int, default=1048576)
    parser.add_argument("--maximum-final-heap-drop", type=int, default=16384)
    parser.add_argument("--maximum-final-psram-drop", type=int, default=131072)
    args = parser.parse_args()
    if args.history_polls < 1:
        parser.error("--history-polls must be at least 1")
    if args.soak_seconds < 0:
        parser.error("--soak-seconds cannot be negative")
    if args.soak_interval <= 0:
        parser.error("--soak-interval must be positive")
    for option in (
        "minimum_free_heap",
        "minimum_free_psram",
        "maximum_final_heap_drop",
        "maximum_final_psram_drop",
    ):
        if getattr(args, option) < 0:
            parser.error(f"--{option.replace('_', '-')} cannot be negative")

    try:
        run(args)
    except (SmokeTestFailure, URLError, TimeoutError) as error:
        print(f"FAIL {error}", file=sys.stderr)
        return 1

    print("PASS passive Heltec WebUI smoke test complete; no transmit confirmation was sent")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
