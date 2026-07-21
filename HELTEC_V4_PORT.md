# Heltec WiFi LoRa 32 V4 port

This checkout adds the `heltec-wifi-lora-32-v4` PlatformIO environment for the
standard ESP32-S3R2 V4 board with 16 MB flash and 2 MB QSPI PSRAM.

The port is based on `BruceDevices/firmware` `main` commit
`ac869d3d99ba222fd2fe7f76b707e4929385bd4c`, verified against the fetched
upstream branch on July 18, 2026.

## Current support

- Native USB serial and USB HID
- Wi-Fi and BLE
- 16 MB flash and 2 MB PSRAM configuration
- Active-low Vext control on GPIO 36
- SX1262 SPI pins: SCK 9, MISO 11, MOSI 10, CS 8, reset 12, busy 13, DIO1 14
- V4 RF front end: power GPIO 7, enable GPIO 2, TX/RX direction GPIO 5
- US915 default LoRa frequency: 915 MHz (editable in LoRa settings)
- GNSS connector: ESP RX 39, ESP TX 38, power control 34 (active low), reset 42, PPS 41, wake 40
- WebUI startup mode with direct `BruceNet` AP initialization (no preceding
  station scan)
- Authenticated WebUI hardware panel with live GPS monitoring and receive-only
  LoRa controls plus a constrained transmitter
- Bounded GPS fix track with JSON and GPX downloads
- Bounded recent LoRa receive history with signal metrics and JSON download
- Reset-resistant LittleFS field logger for incremental GPS fixes and passive
  BLE observations, with optional automatic resume after a reset
- OLED boot screen plus network, login, live GPS, live LoRa RX, and hardware
  status pages plus a field-log status page
- PRG button cycles through all six OLED dashboard pages
- Holding PRG on the GPS or LoRa page toggles GPS monitoring or receive-only
  LoRa listening
- Holding PRG on the field-log page starts or stops a GPS+BLE log with
  automatic reset resume enabled
- Holding PRG for two seconds on the hardware page enters deep sleep after the
  button is released; pressing PRG wakes the board
- Deep-sleep wake using the PRG/BOOT button on GPIO 0

The onboard 128x64 SSD1306 OLED shows boot progress, WebUI connection details,
live GPS state and counters, receive-only LoRa state, packet count, frequency,
last-packet RSSI/SNR and a payload preview, passive hardware diagnostics, and
field-log session/counter status.
Short PRG presses cycle pages; a 0.9-second
hold on the GPS, LoRa, or field-log page toggles that service. Bruce's complete graphical
menu remains in the WebUI because it targets color TFT drivers and multi-button
navigation. A two-second hold on the hardware page enters deep sleep after PRG
is released.

The onboard SX1262 pin mapping, V4 RF front-end controls, and default radio type
are compiled in. Attach the correct antenna before using the radio and configure
a legal frequency and transmit power for your region.

Leaving LoRa chat now puts the SX1262 to sleep, closes its dedicated SPI bus,
and powers down the RF front end. Deep sleep also disables the RF front end,
GNSS power rail, and battery-sense divider. The firmware never transmits during
boot diagnostics.

## Hardware validation

The firmware performs passive boot diagnostics without transmitting an RF
packet. Press PRG four times from the WebUI network page to show the hardware page.
Authenticated WebUI sessions can also read the same machine-readable status at
`/api/heltec/status`.

## WebUI hardware panel

Log in to the WebUI and use the **Heltec WiFi LoRa 32 V4** panel above the file
browser:

- The panel reports firmware identity, uptime, heap/PSRAM health, and provides a
  combined JSON diagnostic download assembled from the authenticated hardware,
  GPS track, LoRa status/history, and field-log status/file-list routes. The snapshot contains no
  WebUI credentials or session token. Because the AP MAC, retained GPS
  coordinates, LoRa payloads, and field-log metadata can be present, the browser shows a privacy
  confirmation before saving the file.
- Authenticated status also identifies the active WebUI access point by SSID,
  IPv4 address, channel, connected-client count, and AP MAC address. It never
  reports the Wi-Fi password.
- The Settings reboot control requires both a browser confirmation and an
  authenticated POST confirmation. The server acknowledges the request before a
  delayed restart, and the browser waits up to 45 seconds for the WebUI to return.
- **Start GPS** powers the GNSS receiver and updates NMEA, satellite, position,
  altitude, speed, and HDOP data every two seconds. **Stop GPS** releases UART2
  and turns the GNSS power rail off. While monitoring, the 16 most recent valid
  fixes are sampled no faster than once every five seconds and can be downloaded
  as JSON or GPX. When the receiver supplies a current UTC date and time, both
  formats include it; GPX descriptions retain a boot-relative capture time as a
  fallback.
- **Start receiver** initializes the SX1262 at the selected frequency and enters
  continuous receive mode. For this US915 build the field is restricted to
  902–928 MHz. The panel shows received packet count, last payload, RSSI, and
  SNR; **Stop receiver** sleeps the radio and powers down the RF front end. The
  eight most recent received packets are retained in memory with RSSI, SNR, and
  boot-relative timestamps and can be downloaded as JSON.
- **Review & transmit** is available only while the receiver is active. It
  sends 1–64 printable ASCII bytes at the active frequency with fixed 2 dBm
  output, requires a browser confirmation for every packet, and enforces a
  five-second cooldown before another WebUI transmission.
- **Reset-resistant field log** independently enables GPS fixes, passive BLE
  observations, and automatic resume. Each observation is serialized as one
  newline-terminated JSON object, flushed, and closed immediately. A reset can
  therefore affect only the record being written. If the prior segment lacks a
  final newline, boot preserves it, starts the next numbered segment, writes a
  `tail_recovery` record, rebuilds the session counters from all complete
  records, and resumes the selected services. BLE scanning is passive; a device
  is recorded at most once per minute, and a GPS position is attached when a fix
  is no more than 30 seconds old. Logging stops before LittleFS free space falls
  below the 256 KiB reserve.
- Saved `session-NNNNNN-SSS.ndjson` segments are listed and downloaded through
  authenticated routes. A segment marked **interrupted tail** contains valid
  newline-delimited records followed by the preserved partial write; consumers
  must ignore that final non-terminated fragment. Downloads require a browser
  privacy confirmation because records can contain precise coordinates, BLE
  addresses, and broadcast device names. The WebUI does not offer deletion, so
  stored evidence is never removed by an accidental control click.

These controls require the normal WebUI session cookie. Their endpoints are
`POST /api/heltec/gps`, `GET|POST /api/heltec/gps/history`,
`GET|POST /api/heltec/lora`,
`GET|POST /api/heltec/lora/history`, `POST /api/heltec/lora/transmit`, and
`GET /api/heltec/status`. Field logging uses `GET|POST /api/heltec/fieldlog`,
`GET /api/heltec/fieldlog/files`, and
`GET /api/heltec/fieldlog/download?name=...`. Starting accepts `gps`, `ble`, and
`autoResume` booleans; stopping requires `action=stop`. The authenticated reboot endpoint is `POST /reboot`;
it requires `action=restart` and the literal confirmation field `RESTART`. The
history POST only accepts `action=clear`. The transmit endpoint additionally
requires the literal confirmation field `TRANSMIT`.

For repeatable two-board testing, the original receiver keeps the `BruceNet`
SSID and the second board uses `BrucePeer`; both retain the `brucenet` password.
Each AP serves `172.0.0.1`, so connect to and verify the intended SSID before
using its authenticated API. Keeping distinct SSIDs is part of the safety check
that prevents operating the wrong radio.

The transmitter is intentionally conservative; the operator is still
responsible for choosing a frequency and usage pattern permitted in their
location. Boot diagnostics and OLED controls never transmit.

Validated on the target board:

- SX1262 SPI initialization and receive-mode entry: RadioLib status `0`
  (`RADIOLIB_ERR_NONE`)
- GNSS UART on GPIO 39/38: NMEA stream detected at 9600 bps
- Authenticated GPS monitor lifecycle: power on, live NMEA counters, power off
- Outdoor GNSS fix: 12 satellites at 0.9 HDOP, with altitude and speed data
- Authenticated GPS track endpoint: the 16-point ring filled with ordered valid
  fixes at the configured five-second minimum interval, remained available after
  GPS power-off, and passed five repeated passive requests
- GNSS UTC export: eight post-flash fixes carried ordered UTC timestamps, and
  the WebUI generated valid GPX with one `<time>` element per track point
- Authenticated SX1262 lifecycle at 915 MHz: receive start/stop status `0`
- One authenticated 13-byte WebUI test packet at 915 MHz and fixed 2 dBm:
  HTTP 200, RadioLib status `0`, and transmit counter advanced from 0 to 1
- Two-board 915 MHz receive test: a second Heltec V4 sent exactly one
  `BRUCE-RX-TEST` packet at fixed 2 dBm; the original retained the exact payload
  at -43 dBm RSSI and 13 dB SNR, with receiver/transmitter counters of 1/0
- Authenticated eight-entry LoRa history endpoint: repeated passive requests
  returned HTTP 200 without a stack fault or reboot
- Authenticated diagnostics status: firmware identity, uptime, heap, and 2 MB
  PSRAM capacity were reported with valid live values; the served WebUI included
  the combined snapshot schema and GPS/LoRa privacy confirmation
- The combined hardware/GPS/LoRa diagnostic JSON contract round-tripped without
  any username, password, cookie, session, or authorization fields
- Live AP telemetry matched the test connection: SSID `BruceNet`, IPv4 address
  `172.0.0.1`, channel 6, and one connected client; the reported AP MAC matched
  the original board identity
- A GPS+BLE field-log session survived an authenticated software reset without
  changing its session or segment. The board reconstructed its counters and
  continued passive scanning, then produced a clean 14,424-byte segment with
  75 complete records: one `session_start`, 72 BLE observations representing
  31 unique addresses, one `session_resume`, and one `session_stop`.
- A second BLE-only lifecycle produced 16 observations and a clean stop. Free
  heap measured 97,008 bytes before scanning, 24,676 bytes while the scanner was
  active, and 96,800 bytes after the logger-owned NimBLE instance was released,
  leaving only a 208-byte difference from baseline.
- Both downloaded field-log segments passed the host validator: 93 complete
  NDJSON records across two sessions, no interrupted tails, and byte sizes that
  exactly matched the authenticated file-list API. The served WebUI contained
  the logger controls and authenticated download support.
- After a firmware upload reset the Preferences high-water mark while LittleFS
  retained sessions 1 and 2, boot reconstructed session 2 from the stored file
  names and allocated session 3 for the next run. All three resulting segments
  passed validation with 117 complete records and no interrupted tails, proving
  upgrades cannot silently append a new session to an existing filename.
- After removing the generic startup station scan, `BruceNet` advertised and
  accepted the Mac connection immediately after each flash without a serial
  Wi-Fi recycle.
- A final 30-second passive soak of the release candidate completed 14 samples
  with no reboot, session/state change, or LoRa transmission. Free heap remained
  at 97,344 bytes, free PSRAM stayed within 2,050,872–2,051,280 bytes, and the
  slowest request cycle took 0.205 seconds.
- A 120-second passive WebUI soak completed 57 four-endpoint samples without a
  reboot, session loss, identity/state/history change, or RF counter change.
  Free heap stayed within 97,396–97,760 bytes, free PSRAM within
  2,050,944–2,051,276 bytes, and the slowest cycle took 0.349 seconds.
- A confirmed WebUI restart returned HTTP 202 before the board went offline,
  then the authenticated API recovered with the same board/AP identity and an
  uptime reset from 1,174,424 ms to 14,179 ms. LoRa returned stopped with a zero
  transmit counter, and the complete passive smoke test passed after recovery.
- All Heltec WebUI status/control requests returned HTTP 200 after login
- A clean Heltec target rebuild succeeded from the local PlatformIO cache with
  the internet service disabled and external DNS unavailable
- OLED, PRG status-page cycling, Wi-Fi AP, WebUI, 16 MB flash, and 2 MB PSRAM
- OLED long-press controls for GPS monitor start/stop and LoRa RX start/stop
- Hardware-page deep-sleep entry after button release and PRG wake
- No invalid `GPIO_NUM_NC` access during boot

The outdoor result confirms both receiver communication and position fixes.
Future time-to-fix still depends on sky view, antenna placement, and satellite
conditions.

## Build

Bruce's current Espressif platform requires Python 3.10 through 3.13. This
checkout has a Python 3.13 virtual environment:

```sh
PATH="$PWD/.venv313/bin:$PATH" pio run -e heltec-wifi-lora-32-v4
```

The merged image is written to `Bruce-heltec-wifi-lora-32-v4.bin` and is
flashed at offset `0x0`.

## Upload

```sh
PATH="$PWD/.venv313/bin:$PATH" pio run -e heltec-wifi-lora-32-v4 -t upload
```

If automatic upload does not start, hold PRG/BOOT, tap RST, release PRG/BOOT,
and retry.

## Passive WebUI smoke test

With the computer connected to `BruceNet`, run the board-specific smoke test:

```sh
python3 boards/heltec-wifi-lora-32-v4/validate_webui.py
```

The test verifies authentication, firmware and memory health, the combined
diagnostic snapshot contract, bounded GPS and LoRa histories, field-log status
and file-list safety, route separation, frequency bounds, and an unchanged transmit counter. It never supplies a LoRa
payload or the literal `TRANSMIT` confirmation, so it does not intentionally
emit an RF packet.

For a sustained passive reliability check, add a soak duration:

```sh
python3 -u boards/heltec-wifi-lora-32-v4/validate_webui.py --soak-seconds 120
```

Soak mode repeatedly checks the authenticated hardware, GPS, LoRa, and field-log routes.
It fails on a reboot or session loss, low memory, changed AP identity, unexpected
GPS/LoRa state or history, or a changed LoRa transmit counter.

Validate downloaded NDJSON segments, or exercise the built-in interrupted-tail
fixture, with:

```sh
python3 boards/heltec-wifi-lora-32-v4/validate_field_log.py session-*.ndjson
python3 boards/heltec-wifi-lora-32-v4/validate_field_log.py --self-test
```
