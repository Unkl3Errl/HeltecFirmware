# Heltec V4 release installation and operation

This guide covers the two supported firmware choices for the 16 MB flash,
2 MB PSRAM Heltec WiFi LoRa 32 V4 and the Android controller that works with
both. Choose one firmware image for each board; the Android app does not replace
firmware.

## Choose the firmware

| Build | Best use | Device interface | Local storage | SX1262 LoRa |
| --- | --- | --- | --- | --- |
| Unified Bruce + Marauder `v0.2.2` | Standalone status, WebUI control, passive Wi-Fi/BLE/GPS field logging, and constrained LoRa work | OLED/PRG, BruceNet WebUI, Android over Wi-Fi, USB serial | Reset-resistant LittleFS NDJSON logs | Receive plus individually confirmed US915 test packets |
| Standalone Marauder `v1.14.0-heltec.2` | Marauder OLED menus and complete upstream USB command line | OLED/PRG, Android or terminal over USB | USB session/capture streaming; no removable card | Dormant and not initialized |

The unified build incorporates the Marauder-derived passive Wi-Fi survey, so
most field-survey work does not require changing firmware. Use standalone
Marauder when its full USB command set or its dedicated OLED workflow is the
priority.

## Permanent releases

Download only from these release pages:

- [Unified Bruce + Marauder v0.2.2](https://github.com/Unkl3Errl/HeltecFirmware/releases/tag/v0.2.2)
- [Standalone Marauder v1.14.0-heltec.2](https://github.com/Unkl3Errl/ESP32Marauder/releases/tag/v1.14.0-heltec.2)
- [Heltec Controller for Android v0.3.3](https://github.com/Unkl3Errl/HeltecFirmware/releases/tag/android-v0.3.3)

Verify downloaded files before flashing or installing:

| File | SHA-256 |
| --- | --- |
| `Bruce-heltec-wifi-lora-32-v4-app.bin` | `632d5a193093cc8d210fdea0f7df387ecce6f52d46dd4a7605b2f4a78724c53d` |
| `Bruce-heltec-wifi-lora-32-v4.bin` | `17ce0f2455b0130d204d3d509c10bec4ee5ec1e568203b622746fcfad906f76d` |
| `ESP32Marauder-Heltec-V4-v1.14.0-heltec.2.bin` | `4d28aa5aa985e0b08814626a38adc8b290ef0ff51f752e38a780e05c7c01f365` |
| `HeltecController-v0.3.3.apk` | `f4fdbfad30af6485faf9ca360c263e83ac8e7bfbcbc51b52624e9f21efbd7ea6` |

On macOS or Linux:

```sh
shasum -a 256 DOWNLOAD_NAME
```

## Before flashing

1. Use a data-capable USB cable and identify the exact connected board before
   writing flash.
2. Power the board off before attaching or removing the LoRa or GNSS antenna.
3. Export any Bruce field logs and any Marauder/Android USB sessions that must
   be retained.
4. Install `esptool` in a Python environment and substitute the real serial
   port for `/dev/cu.usbmodemXXXX` in the commands below.

Do not use an application-only image to change firmware families. Bruce and
standalone Marauder do not share an application contract, partition ownership,
or filesystem format.

## Upgrade an existing unified Bruce installation

When the board already runs this repository's compatible Bruce partition
layout, flash the application-only image at `0x10000`:

```sh
esptool --chip esp32s3 --port /dev/cu.usbmodemXXXX --baud 460800 \
  write-flash 0x10000 Bruce-heltec-wifi-lora-32-v4-app.bin
```

This preserves NVS settings and LittleFS field logs. After restart, the device
and Android system panel must report firmware `0.2.2` and commit
`84af0798e63b`.

## Factory-flash or switch to unified Bruce

Use the merged image at `0x0` for a new board, recovery, or a switch from
standalone Marauder:

```sh
esptool --chip esp32s3 --port /dev/cu.usbmodemXXXX --baud 460800 \
  write-flash 0x0 Bruce-heltec-wifi-lora-32-v4.bin
```

The merged image resets NVS settings. It ends before the Bruce LittleFS
partition, but data left by another partition layout must not be assumed to be
valid or accessible. A separate full-chip erase also removes LittleFS.

Normal startup shows `BRUCE / HELTEC V4`, starts the WebUI, and advertises
`BruceNet`. The release defaults are:

| Setting | Default |
| --- | --- |
| Device Wi-Fi password | `brucenet` |
| WebUI address | `http://172.0.0.1` |
| WebUI username | `admin` |
| WebUI password | `bruce` |

Change the credentials before use outside a controlled test environment.

## Factory-flash or switch to standalone Marauder

Use the Marauder merged image at `0x0`:

```sh
esptool --chip esp32s3 --port /dev/cu.usbmodemXXXX --baud 460800 \
  write-flash 0x0 ESP32Marauder-Heltec-V4-v1.14.0-heltec.2.bin
```

Treat this as a factory switch: export Bruce logs first and do not rely on data
from the previous firmware's partitions. Standalone controls are:

- One PRG press: next item.
- Long press, about 0.9 seconds: select, start, or confirm.
- Two PRG presses: return, cancel, or stop.
- The first press after display timeout only wakes the OLED.

The System menu provides an always-on or 15/30/45/60-second display timeout,
PRG-wake sleep, and a minimum-power `Power down` deep-sleep mode. The board has
no software-controlled power latch, so that mode is not a physical battery
disconnect. The physical RST switch is wired directly to the ESP32-S3 enable
pin and cannot be debounced by firmware.

## Bootloader recovery

If the serial port is present but flashing cannot connect:

1. Hold PRG/BOOT.
2. Tap RST while continuing to hold PRG/BOOT.
3. Release PRG/BOOT.
4. Run the selected merged-image flash command again.

Resolve the serial port again after entering the bootloader; its device name
can change. Never select a port only because it resembles the previous name.

## Android controller

Install the signed `HeltecController-v0.3.3.apk`. Its package name is
`com.unkl3errl.helteccontroller`, and its permanent signing-certificate SHA-256
is:

```text
15:17:B9:22:56:7D:55:7E:9E:71:B5:4A:14:1C:48:56:27:FD:27:50:CF:BC:F8:D1:40:75:C6:D0:AF:37:7C:A4
```

- For unified Bruce, join BruceNet in the app and use the authenticated Wi-Fi
  interface. The phone can keep a separate internet transport when Android
  supports concurrent local-only Wi-Fi.
- For standalone Marauder, connect the board directly to the phone with a
  data-capable USB OTG cable and approve USB permission.
- One phone can use Bruce over Wi-Fi while a Marauder board is attached over
  USB. Confirm the selected app tab and physical board before starting a tool.

The app saves Bruce field-log exports through Android's document picker and
records Marauder USB sessions in private app storage until the user exports or
deletes them.

## Passive post-flash checks

These checks do not require a radio transmission:

1. Confirm the expected startup screen and firmware identity.
2. Confirm one-press, double-press, and long-press behavior without unexpected
   display blanking.
3. For Bruce, log in through Android or the WebUI and verify GPS/BLE/Wi-Fi
   status, field-log inventory, and `LoRa idle` before any radio action.
4. For Marauder, connect USB serial at 115200 baud, run `help`, and exercise
   only the passive scan/list/stop workflow needed for verification.
5. Export a short test log or USB session and confirm that it is saved on the
   phone before depending on the device in the field.

Detailed Bruce pin mapping, endpoints, and validation are in
[HELTEC_V4_PORT.md](HELTEC_V4_PORT.md). Standalone Marauder behavior is in
[the Marauder Heltec V4 guide](https://github.com/Unkl3Errl/ESP32Marauder/blob/master/HELTEC_V4.md),
and Android details are in
[android/HeltecController/README.md](android/HeltecController/README.md).

Use these tools only on systems, networks, and frequencies you own or are
explicitly authorized to test. The operator remains responsible for applicable
radio rules.
