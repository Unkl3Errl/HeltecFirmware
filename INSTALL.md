# Heltec V4 installation and operation

This guide covers the unified Bruce firmware, the separate standalone Marauder
firmware, and the shared Android companion for the 16 MB flash, 2 MB PSRAM
Heltec WiFi LoRa 32 V4.

## Permanent releases

Download Bruce firmware from the
[HeltecFirmware releases](https://github.com/Unkl3Errl/HeltecFirmware/releases)
and the Android app from the
[Android v0.3.3 release](https://github.com/Unkl3Errl/HeltecFirmware/releases/tag/android-v0.3.3).
The standalone Marauder image is published in the
[ESP32Marauder Heltec releases](https://github.com/Unkl3Errl/ESP32Marauder/releases).
Release notes publish SHA-256 hashes for each artifact. Verify a download with:

```sh
shasum -a 256 DOWNLOAD_NAME
```

## Before flashing

1. Export any field logs that must be retained.
2. Use a data-capable USB cable and resolve the exact serial port.
3. Power the board off before attaching or removing LoRa or GNSS antennas.
4. Install `esptool` in a compatible Python environment.

## Upgrade an existing Bruce installation

For a board already using this repository's partition layout, flash the
application-only image at `0x10000` to preserve NVS settings and LittleFS logs:

```sh
esptool --chip esp32s3 --port /dev/cu.usbmodemXXXX --baud 460800 \
  write-flash 0x10000 Bruce-heltec-wifi-lora-32-v4-app.bin
```

## Factory or recovery flash

Write the merged image at `0x0` for a new board or recovery:

```sh
esptool --chip esp32s3 --port /dev/cu.usbmodemXXXX --baud 460800 \
  write-flash 0x0 Bruce-heltec-wifi-lora-32-v4.bin
```

The merged image resets NVS settings. A separate full-chip erase also removes
LittleFS field logs.

## Install standalone Marauder

Use the standalone image only when the board should run Marauder's OLED/PRG
interface instead of BruceNet. Download
`ESP32Marauder-Heltec-V4-v1.14.0-heltec.2.bin` from the
[Marauder release](https://github.com/Unkl3Errl/ESP32Marauder/releases/tag/v1.14.0-heltec.2)
and write it at `0x0`:

```sh
esptool --chip esp32s3 --port /dev/cu.usbmodemXXXX --baud 460800 \
  write-flash 0x0 ESP32Marauder-Heltec-V4-v1.14.0-heltec.2.bin
```

SHA-256:

```text
4d28aa5aa985e0b08814626a38adc8b290ef0ff51f752e38a780e05c7c01f365
```

The standalone PRG controls are one press for next, two presses for
back/cancel/stop, and a 0.9-second hold for select/start/confirm. Its System
menu includes display timeout choices plus sleep and power-down deep sleep.
It does not initialize the SX1262 LoRa radio and has no microSD slot.

Do not use Bruce's application-only image on Marauder, or Marauder's image on
Bruce. Switching firmware requires its merged image at `0x0`; back up any
LittleFS/SPIFFS data first.

Normal startup shows `BRUCE / HELTEC V4`, starts the WebUI, and advertises
`BruceNet`. Defaults are:

| Setting | Default |
| --- | --- |
| Device Wi-Fi password | `brucenet` |
| WebUI address | `http://172.0.0.1` |
| WebUI username | `admin` |
| WebUI password | `bruce` |

Change the credentials before use outside a controlled test environment.

## Bootloader recovery

If flashing cannot connect, hold PRG/BOOT, tap RST, release PRG/BOOT, resolve
the serial port again, and retry the merged-image command.

## Android companion

The shared app has package name `com.unkl3errl.helteccontroller`, preserving
update continuity with prior signed releases. It identifies Bruce or Marauder
through read-only USB probes and unlocks only the matching interface. Bruce can
also be detected through BruceNet. After detection, approve Android's local
network request and log in with the WebUI credentials. The app can export field
logs, supply phone GPS fixes to an active GPS log, render and navigate Bruce's
remote vector display, manage LittleFS files, or connect directly to the
device's 115200-baud USB CDC console through a data-capable OTG cable.

## Passive post-flash checks

1. Confirm the startup screen and firmware identity.
2. Log in through Android or the WebUI.
3. Verify GPS, BLE field-log inventory, and `LoRa idle` before radio work.
4. Create and export a short GPS/BLE test log.

Detailed pin mapping, endpoints, and validation are in
[HELTEC_V4_PORT.md](HELTEC_V4_PORT.md). Use these tools only on systems,
networks, and frequencies you own or are explicitly authorized to test.
