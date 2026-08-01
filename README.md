# Bruce for Heltec WiFi LoRa 32 V4

This repository is a Heltec WiFi LoRa 32 V4-only firmware project derived from
[BruceDevices/firmware](https://github.com/BruceDevices/firmware). Its build,
board metadata, automated checks, and documentation target only the ESP32-S3
Heltec V4 with 16 MB flash, 2 MB PSRAM, onboard SSD1306 OLED, SX1262 radio, and
the attached GNSS module. This project contains the Bruce firmware only; the
shared Android companion is maintained separately.

## Releases and installation

Use [INSTALL.md](INSTALL.md) for permanent download links and hashes,
data-preserving upgrades, factory flashing, bootloader recovery, Android
installation, and passive post-flash checks.

## Heltec V4 integration

- Marauder-style standalone OLED interface with scrolling nested menus,
  inverted selection highlighting, and the same PRG gestures: one press moves
  to the next item, two presses return, and a 0.9-second hold selects.
- On-device controls for Bruce's supported GPS monitor, receive-only LoRa,
  GPS+BLE field logger, diagnostics, persisted 15/30/45/60-second display
  timeouts (or always on), confirmed PRG-wake sleep, and confirmed power down.
- Authenticated WebUI hardware, network, memory, GPS, and LoRa diagnostics.
- Live GNSS monitoring with a bounded 16-fix track and JSON/GPX export.
- Reset-resistant LittleFS field logging that appends onboard or Android GPS
  fixes plus passive BLE observations as NDJSON, resumes after a
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

## Android controller

The canonical Android companion is
[`Unkl3Errl/HeltecController`](https://github.com/Unkl3Errl/HeltecController).
It identifies Bruce or Marauder at runtime and enables only the matching
interface. For Bruce it uses the authenticated BruceNet WebUI API, exports
field logs through Android's document picker, and can supply explicitly
labeled phone GPS fixes while logging. It also provides a native Bruce
vector-display remote, LittleFS file manager, and guarded USB CDC console for
the original Bruce command set.

The app keeps device credentials in memory, routes Bruce requests through the
selected local-only Wi-Fi network, and requires confirmation for transmitting
or state-changing operations. The app repository documents requirements,
supported features, detection behavior, and build instructions.

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

The build produces two images:

- `Bruce-heltec-wifi-lora-32-v4.bin` is the merged factory image for offset
  `0x0`.
- `Bruce-heltec-wifi-lora-32-v4-app.bin` is the application-only upgrade image
  for offset `0x10000`. Use it to preserve NVS settings and LittleFS data when
  upgrading from a release with the same partition layout.

Firmware builds are self-identifying. Untagged builds report version `dev` and
the current short Git commit; exact `vX.Y.Z` firmware tags report version
`X.Y.Z`. A modified local checkout appends `-dirty` to its commit identity.
Release automation can set `HELTEC_FIRMWARE_VERSION` and
`HELTEC_FIRMWARE_COMMIT` explicitly. Verify that the resolved values are
actually embedded in the merged image with:

```sh
python3 boards/heltec-wifi-lora-32-v4/validate_firmware_metadata.py \
  Bruce-heltec-wifi-lora-32-v4.bin
```

## Flash

For an in-place application upgrade that preserves settings and data:

```sh
esptool --chip esp32s3 --port /dev/cu.usbmodem101 write-flash \
  0x10000 Bruce-heltec-wifi-lora-32-v4-app.bin
```

For a normal source upload, PlatformIO writes the bootloader, partition table,
and application at their individual offsets without filling the NVS gap:

```sh
pio run -e heltec-wifi-lora-32-v4 -t upload --upload-port /dev/cu.usbmodem101
```

For a factory or recovery flash, write the merged image at offset `0x0`:

```sh
esptool --chip esp32s3 --port /dev/cu.usbmodem101 write-flash \
  0x0 Bruce-heltec-wifi-lora-32-v4.bin
```

The merged image fills the NVS region with erased bytes and therefore resets
saved settings. It ends before the LittleFS partition in this layout, but a
separate full-chip erase also removes LittleFS field logs and other stored data.

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
copyrights and attribution.
