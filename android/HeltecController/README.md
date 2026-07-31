# Heltec Controller for Android

Heltec Controller is a native Android interface for the two Heltec WiFi LoRa 32
V4 firmware builds in this workspace:

- **Unified Bruce + Marauder:** authenticated control, telemetry, passive
  Wi-Fi discovery, and GPS/BLE/Wi-Fi field logging over the device's local
  Wi-Fi access point.
- **Legacy Marauder:** the separate firmware's existing 115200-baud command interface over
  Android USB host/OTG.

The app does not add a new remote-control service to either firmware. It uses
the interfaces that the current builds already expose.

## Requirements

- Android 10 (API 29) or newer.
- A phone that supports USB host mode and a data-capable USB-C OTG connection
  for Marauder.
- Heltec Firmware `v0.2.1` or newer running in WebUI mode for BruceNet.
- ESP32 Marauder `v1.14.0-heltec.2` or newer running on the USB-connected
  device.

## BruceNet connection

1. Open **BruceNet** in the app.
2. Tap **Join device Wi-Fi** and approve Android's network request.
3. Tap **Login** if login does not begin automatically.

Defaults used by the current firmware are:

| Setting | Default |
| --- | --- |
| Wi-Fi SSID | `BruceNet` |
| Wi-Fi password | `brucenet` |
| WebUI URL | `http://172.0.0.1` |
| WebUI username | `admin` |
| WebUI password | `bruce` |

The app requests BruceNet as a local-only peer network and routes only Bruce API
requests through it. On Android phones that support concurrent local-only Wi-Fi,
mobile data or the primary internet network can remain available. Android still
controls whether concurrency is available on a particular phone.

The Bruce interface includes:

- Board, firmware, battery, memory, Wi-Fi, BLE, and live GPS status.
- GPS monitor control and recent track inspection.
- Marauder-derived receive-only Wi-Fi discovery with a bounded AP list.
- Reset-resistant GPS/BLE/Wi-Fi field logger control, file inventory, and authenticated
  NDJSON export through Android's document picker.
- Optional phone GPS assistance. While an active field log has GPS selected,
  Android can append a fix every five seconds through the authenticated API;
  those records are explicitly marked with `source: "android"`.
- SX1262 receiver control, receive history, and current radio status.
- Firmware-limited LoRa transmission with typed `TRANSMIT` confirmation.
- Guarded restart and a link to the full built-in WebUI.

## Marauder connection

1. Connect the Marauder Heltec to the Android phone with an OTG adapter or OTG
   cable. The phone must be the USB host.
2. Open **Marauder** in the app and tap **Connect**.
3. Approve Android's USB permission dialog.

The flashed Heltec enumerates as Espressif native USB JTAG/serial with VID
`0x303A` and PID `0x1001`. The app also probes other CDC-ACM serial devices so a
USB bridge can be used if the hardware configuration changes.

The Marauder interface includes:

- A live, selectable USB serial console.
- Quick actions for help, GPS, Wi-Fi/BLE discovery, list output, and stopping a
  running scan.
- The complete upstream command line through the command input.
- A command safety classifier. Transmit/state-changing commands require typed
  `AUTHORIZE` confirmation; unknown future commands require a review dialog.

The standalone OLED controls in `v1.14.0-heltec.2` are one PRG press for the
next item, a long press (about 0.9 seconds) to select/start/confirm, and two
presses to return/cancel/stop. If the inactivity timer has blanked the display,
the first press only wakes it and is not also treated as navigation.

Marauder Bluetooth scanning is a firmware feature, not an app transport. The
current firmware does not advertise a BLE UART/controller service, so USB is
required for app control.

## Security and operating limits

- WebUI and Wi-Fi passwords are kept in memory and are not persisted by the app.
- Phone location is sent only to the selected device over its local link while
  phone GPS assist is visibly enabled; it is not uploaded to an internet service.
- Bruce uses cleartext HTTP because the isolated ESP32 access point exposes an
  HTTP-only WebUI. The app does not upload device data to an internet service.
- Radio and network testing must be limited to systems and spectrum you own or
  have explicit authorization to test.
- LoRa transmit is still constrained by the Bruce firmware's frequency range,
  printable payload length, +2 dBm power, and cooldown checks.

## Build

Open this directory in Android Studio, allow Gradle to synchronize, and build
the `app` configuration. From a terminal with Android SDK 35 installed:

```sh
./gradlew testDebugUnitTest assembleDebug assembleRelease
```

The debug APK is written to
`app/build/outputs/apk/debug/app-debug.apk`. Without signing variables, the
release task intentionally produces
`app/build/outputs/apk/release/app-release-unsigned.apk`.

For a signed release, provide all four values through the process environment:

```sh
export HELTEC_RELEASE_STORE_FILE=/secure/path/HeltecController-release.p12
export HELTEC_RELEASE_STORE_PASSWORD='from a secure secret store'
export HELTEC_RELEASE_KEY_ALIAS=heltec-controller
export HELTEC_RELEASE_KEY_PASSWORD="$HELTEC_RELEASE_STORE_PASSWORD"
./gradlew assembleRelease
```

Do not put literal signing passwords or the private keystore in this repository.
The signed APK is written to
`app/build/outputs/apk/release/app-release.apk`. The permanent public
certificate identity and continuity requirements are documented in
[`SIGNING.md`](SIGNING.md).

The app is configured with package ID `com.unkl3errl.helteccontroller`, minimum
API 29, target API 35, and version `0.3.1`.
