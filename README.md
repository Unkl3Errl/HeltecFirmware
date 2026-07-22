# Unified Bruce + Marauder for Heltec WiFi LoRa 32 V4

This repository is a Heltec WiFi LoRa 32 V4-only firmware project derived from
[BruceDevices/firmware](https://github.com/BruceDevices/firmware). Its build,
board metadata, automated checks, and documentation target only the ESP32-S3
Heltec V4 with 16 MB flash, 2 MB PSRAM, onboard SSD1306 OLED, SX1262 radio, and
the attached GNSS module. A bounded, passive Wi-Fi survey adapted from
[ESP32 Marauder](https://github.com/justcallmekoko/ESP32Marauder) is integrated
into Bruce's existing display, WebUI, radio, storage, and authentication
lifecycles instead of running a second firmware stack.

## Heltec V4 integration

- Seven-page OLED status dashboard with PRG-button navigation, controls, and
  deep-sleep entry.
- Receive-only Marauder Wi-Fi survey with a 64-network device list, manual
  scanning, and reset-resistant automatic field surveys that preserve the
  BruceNet access point.
- Authenticated WebUI hardware, network, memory, GPS, and LoRa diagnostics.
- Live GNSS monitoring with a bounded 16-fix track and JSON/GPX export.
- Reset-resistant LittleFS field logging that appends onboard or Android GPS
  fixes plus passive BLE and Wi-Fi observations as NDJSON, resumes after a
  reset when enabled, and exposes authenticated status and downloads in the
  WebUI and Android app.
- SX1262 receive control with an eight-packet history and JSON export.
- Constrained US915 WebUI transmission: 902–928 MHz, fixed 2 dBm, printable
  payloads up to 64 bytes, per-packet confirmation, and cooldown.
- Delayed, acknowledged WebUI restart with browser recovery polling.
- Passive hardware smoke and soak validation that never supplies an RF
  transmit confirmation.
- Heltec-only capability surface: unsupported infrared, generic Sub-GHz/CC1101,
  RFID/NFC, and NRF24 implementations are removed from the source tree, serial
  CLI, JavaScript runtime, configuration schema, startup hooks, bundled assets,
  and dependencies. The onboard SX1262 remains available through LoRa.
- External Ethernet, FM, iButton, audio, microphone, QR/TFT rendering, and
  Megalodon implementations are removed rather than merely hidden. SD remains
  represented only by the shared filesystem abstraction; this board uses
  LittleFS for local storage.

The complete pin map, endpoint contract, safety constraints, and target-board
validation record are in [HELTEC_V4_PORT.md](HELTEC_V4_PORT.md).

## Build

Bruce's Espressif platform requires Python 3.10 through 3.13. With PlatformIO
available in a compatible environment, run:

```sh
pio run
```

The project exposes only `heltec-wifi-lora-32-v4`, so the explicit equivalent
is:

```sh
pio run -e heltec-wifi-lora-32-v4
```

The merged flash image is written to `Bruce-heltec-wifi-lora-32-v4.bin`.

## Flash

```sh
pio run -e heltec-wifi-lora-32-v4 -t upload --upload-port /dev/cu.usbmodem101
```

Adjust the serial port for the connected board. If automatic upload does not
start, hold PRG/BOOT, tap RST, release PRG/BOOT, and retry.

## WebUI

After boot, connect to `BruceNet` using password `brucenet`, then open
`http://172.0.0.1`. The default WebUI credentials are `admin` / `bruce`.
Change credentials before using the device outside a controlled test setup.

Run the non-transmitting WebUI regression with the computer connected to the
board access point:

```sh
python3 boards/heltec-wifi-lora-32-v4/validate_webui.py
```

Downloaded field-log segments can be checked offline, including recovery after
an interrupted final write:

```sh
python3 boards/heltec-wifi-lora-32-v4/validate_field_log.py ~/Downloads/session-*.ndjson
```

## Safety and legal use

Use this firmware only on systems, frequencies, and networks you own or are
explicitly authorized to test. The operator is responsible for local radio
rules. Passive diagnostics and the supplied validator do not intentionally
transmit LoRa packets; WebUI transmission requires an explicit confirmation for
every packet.

Bruce and this derivative are distributed under the GNU Affero General Public
License v3.0. Upstream authors and contributors retain their respective
copyrights and attribution. The incorporated ESP32 Marauder survey behavior is
adapted from MIT-licensed upstream code and retains attribution in its source.
