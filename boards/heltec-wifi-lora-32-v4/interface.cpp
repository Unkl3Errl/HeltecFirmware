#include "core/powerSave.h"
#include "core/settings.h"
#include "field_logger.h"
#if !defined(LITE_VERSION)
#include "modules/lora/LoRaRF.h"
#endif
#include <ArduinoJson.h>
#include <cstdlib>
#include <driver/gpio.h>
#include <esp32-hal-psram.h>
#include <RadioLib.h>
#include <SPI.h>
#include <TinyGPS++.h>
#include <interface.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <Wire.h>

#if !defined(LITE_VERSION)
bool heltecV4ToggleLoraReceiver(float frequencyMHz);
void heltecV4PollLoraReceiver();
#endif

namespace {
constexpr gpio_num_t kVextPin = GPIO_NUM_36;
constexpr gpio_num_t kButtonPin = GPIO_NUM_0;
constexpr uint8_t kOledResetPin = 21;
constexpr uint8_t kOledClockPin = 18;
constexpr uint8_t kOledDataPin = 17;
constexpr gpio_num_t kBatteryAdcPin = GPIO_NUM_1;
constexpr gpio_num_t kBatteryAdcControlPin = GPIO_NUM_37;
constexpr uint32_t kDebounceMs = 30;
constexpr uint32_t kClickWindowMs = 550;
constexpr uint32_t kLongPressMs = 900;
constexpr size_t kGpsTrackCapacity = 16;
constexpr uint32_t kGpsTrackMinimumIntervalMs = 5000;
#ifdef LORA_DEFAULT_FREQUENCY_HZ
constexpr float kPassiveRadioFrequencyMHz = static_cast<float>(LORA_DEFAULT_FREQUENCY_HZ) / 1000000.0f;
#else
constexpr float kPassiveRadioFrequencyMHz = 915.0f;
#endif

U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, kOledResetPin);
volatile bool oledUiReady = false;
bool webUiApMode = true;
uint8_t statusPage = 0;
int16_t radioDiagnostic = RADIOLIB_ERR_UNKNOWN;
int16_t radioReceiveDiagnostic = RADIOLIB_ERR_UNKNOWN;
uint32_t gpsDiagnosticBytes = 0;
uint16_t gpsDiagnosticSentences = 0;
uint32_t gpsDiagnosticSatellites = 0;
bool gpsDiagnosticFix = false;
String oledActionMessage;
uint8_t oledActionPage = 0;
uint32_t oledActionMessageMs = 0;

enum class OledScreen : uint8_t {
    Boot,
    Root,
    Dashboard,
    Gps,
    Lora,
    FieldLog,
    System,
    Display,
    Status,
    Confirm,
    DeviceInfo,
    Help,
};

enum class OledGesture : uint8_t {
    Single,
    Double,
    Long,
};

enum OledAction : int16_t {
    kOpenDashboard = -1,
    kOpenGps = -2,
    kOpenLora = -3,
    kOpenFieldLog = -4,
    kOpenSystem = -5,
    kOpenDisplay = -6,
    kShowNetwork = -7,
    kShowLogin = -8,
    kShowGps = -9,
    kShowLora = -10,
    kShowFieldLog = -11,
    kShowHardware = -12,
    kShowDeviceInfo = -13,
    kShowHelp = -14,
    kToggleGps = -15,
    kToggleLora = -16,
    kStartFieldLog = -17,
    kStopFieldLog = -18,
    kTimeoutOff = -19,
    kTimeout15 = -20,
    kTimeout30 = -21,
    kTimeout45 = -22,
    kTimeout60 = -23,
    kSleep = -24,
    kPowerDown = -25,
    kBack = -26,
};

struct OledMenuItem {
    const char *label;
    int16_t action;
    bool requiresConfirmation;
};

constexpr OledMenuItem kRootMenu[] = {
    {"Dashboard", kOpenDashboard, false},
    {"GPS monitor", kOpenGps, false},
    {"LoRa receiver", kOpenLora, false},
    {"Field logger", kOpenFieldLog, false},
    {"System", kOpenSystem, false},
};

constexpr OledMenuItem kDashboardMenu[] = {
    {"Network", kShowNetwork, false},
    {"WebUI login", kShowLogin, false},
    {"GPS status", kShowGps, false},
    {"LoRa status", kShowLora, false},
    {"Field-log status", kShowFieldLog, false},
    {"Hardware", kShowHardware, false},
    {"Back", kBack, false},
};

constexpr OledMenuItem kGpsMenu[] = {
    {"Status", kShowGps, false},
    {"Toggle monitor", kToggleGps, false},
    {"Back", kBack, false},
};

constexpr OledMenuItem kLoraMenu[] = {
    {"Status", kShowLora, false},
    {"Toggle receiver", kToggleLora, false},
    {"Back", kBack, false},
};

constexpr OledMenuItem kFieldLogMenu[] = {
    {"Status", kShowFieldLog, false},
    {"Start GPS+BLE+WiFi", kStartFieldLog, false},
    {"Stop logging", kStopFieldLog, false},
    {"Back", kBack, false},
};

constexpr OledMenuItem kSystemMenu[] = {
    {"Network info", kShowNetwork, false},
    {"WebUI login", kShowLogin, false},
    {"Hardware status", kShowHardware, false},
    {"Device info", kShowDeviceInfo, false},
    {"Display timeout", kOpenDisplay, false},
    {"Button help", kShowHelp, false},
    {"Sleep (PRG wake)", kSleep, true},
    {"Power down", kPowerDown, true},
    {"Back", kBack, false},
};

constexpr OledMenuItem kDisplayMenu[] = {
    {"Always on", kTimeoutOff, false},
    {"Turn off after 15s", kTimeout15, false},
    {"Turn off after 30s", kTimeout30, false},
    {"Turn off after 45s", kTimeout45, false},
    {"Turn off after 60s", kTimeout60, false},
    {"Back", kBack, false},
};

OledScreen oledScreen = OledScreen::Boot;
OledScreen oledReturnScreen = OledScreen::Root;
uint8_t oledSelected = 0;
uint8_t oledClickCount = 0;
bool oledRawPressed = false;
bool oledStablePressed = false;
bool oledLongDispatched = false;
bool oledWakePress = false;
bool oledScreenBlanked = false;
uint8_t oledScreenTimeoutSeconds = 60;
uint32_t oledRawChangedAt = 0;
uint32_t oledPressedAt = 0;
uint32_t oledClickDeadline = 0;
uint32_t oledLastInteraction = 0;
uint32_t oledLastRender = 0;
int16_t oledPendingAction = 0;
const char *oledPendingLabel = nullptr;
String oledNotice;
uint32_t oledNoticeUntil = 0;
TaskHandle_t oledInputTaskHandle = nullptr;

void renderOledUi();
void pollOledButton(uint32_t now);
void oledInputTask(void *parameter);
void dispatchOledGesture(OledGesture gesture);
void selectOledItem();
void goBackOled();
[[noreturn]] void enterHeltecDeepSleep(bool wakeOnButton);

struct GpsLiveStatus {
    uint32_t bytes = 0;
    uint32_t sentences = 0;
    uint32_t lastDataMs = 0;
    uint32_t lastFixMs = 0;
    uint32_t satellites = 0;
    double latitude = 0.0;
    double longitude = 0.0;
    double altitudeMeters = 0.0;
    double speedKmph = 0.0;
    double hdop = 0.0;
};

struct GpsTrackPoint {
    uint32_t sequence = 0;
    uint32_t capturedAtMs = 0;
    uint32_t satellites = 0;
    uint16_t utcYear = 0;
    uint8_t utcMonth = 0;
    uint8_t utcDay = 0;
    uint8_t utcHour = 0;
    uint8_t utcMinute = 0;
    uint8_t utcSecond = 0;
    uint8_t utcCentisecond = 0;
    bool utcValid = false;
    double latitude = 0.0;
    double longitude = 0.0;
    double altitudeMeters = NAN;
    double speedKmph = NAN;
    double hdop = NAN;
};

GpsLiveStatus gpsLiveStatus;
GpsTrackPoint gpsTrack[kGpsTrackCapacity] = {};
size_t gpsTrackCount = 0;
size_t gpsTrackNext = 0;
uint32_t gpsTrackSequence = 0;
uint32_t gpsTrackLastCaptureMs = 0;
portMUX_TYPE gpsLiveMux = portMUX_INITIALIZER_UNLOCKED;
TaskHandle_t gpsMonitorTaskHandle = nullptr;
volatile bool gpsMonitorRunning = false;
volatile bool gpsMonitorStopRequested = false;
volatile bool gpsExclusiveUse = false;
volatile bool gpsPowerEnabled = false;

void setGpsPower(bool enabled) {
    pinMode(GPS_POWER_PIN, OUTPUT);
    digitalWrite(
        GPS_POWER_PIN,
        enabled ? GPS_POWER_ACTIVE : (GPS_POWER_ACTIVE == LOW ? HIGH : LOW)
    );
    gpsPowerEnabled = enabled;
}

void resetGpsLiveStatus() {
    portENTER_CRITICAL(&gpsLiveMux);
    gpsLiveStatus = {};
    portEXIT_CRITICAL(&gpsLiveMux);
}

void gpsMonitorTask(void *parameter) {
    (void)parameter;
    setGpsPower(true);
    delay(100);

    HardwareSerial serial(2);
    TinyGPSPlus parser;
    serial.begin(
        bruceConfigPins.gpsBaudrate,
        SERIAL_8N1,
        bruceConfigPins.gps_bus.rx,
        bruceConfigPins.gps_bus.tx
    );
    gpsMonitorRunning = true;
    uint32_t bytes = 0;
    uint32_t sentences = 0;
    uint32_t lastSnapshotMs = 0;

    while (!gpsMonitorStopRequested) {
        bool receivedData = false;
        while (serial.available()) {
            const char c = (char)serial.read();
            parser.encode(c);
            bytes++;
            if (c == '\n') sentences++;
            receivedData = true;
        }

        const uint32_t now = millis();
        if (receivedData || now - lastSnapshotMs >= 500) {
            bool capturedTrackPoint = false;
            HeltecFieldGpsRecord fieldRecord;
            const bool locationUpdated = parser.location.isUpdated();
            const bool validLocation = parser.location.isValid();
            const double latitude = validLocation ? parser.location.lat() : 0.0;
            const double longitude = validLocation ? parser.location.lng() : 0.0;
            const double altitudeMeters = parser.altitude.isValid() ? parser.altitude.meters() : NAN;
            const double speedKmph = parser.speed.isValid() ? parser.speed.kmph() : NAN;
            const double hdop = parser.hdop.isValid() ? parser.hdop.hdop() : NAN;
            const uint32_t satellites =
                parser.satellites.isValid() ? parser.satellites.value() : 0;
            const bool utcValid =
                parser.date.isValid() && parser.time.isValid() && parser.date.age() <= 2000 &&
                parser.time.age() <= 2000;
            const uint16_t utcYear = utcValid ? parser.date.year() : 0;
            const uint8_t utcMonth = utcValid ? parser.date.month() : 0;
            const uint8_t utcDay = utcValid ? parser.date.day() : 0;
            const uint8_t utcHour = utcValid ? parser.time.hour() : 0;
            const uint8_t utcMinute = utcValid ? parser.time.minute() : 0;
            const uint8_t utcSecond = utcValid ? parser.time.second() : 0;
            const uint8_t utcCentisecond = utcValid ? parser.time.centisecond() : 0;
            portENTER_CRITICAL(&gpsLiveMux);
            gpsLiveStatus.bytes = bytes;
            gpsLiveStatus.sentences = sentences;
            if (receivedData) gpsLiveStatus.lastDataMs = now;
            if (parser.satellites.isValid()) gpsLiveStatus.satellites = satellites;
            if (validLocation) {
                gpsLiveStatus.latitude = latitude;
                gpsLiveStatus.longitude = longitude;
                if (locationUpdated) gpsLiveStatus.lastFixMs = now;
            }
            if (!isnan(altitudeMeters)) gpsLiveStatus.altitudeMeters = altitudeMeters;
            if (!isnan(speedKmph)) gpsLiveStatus.speedKmph = speedKmph;
            if (!isnan(hdop)) gpsLiveStatus.hdop = hdop;
            if (
                locationUpdated && validLocation &&
                (gpsTrackLastCaptureMs == 0 || now - gpsTrackLastCaptureMs >= kGpsTrackMinimumIntervalMs)
            ) {
                GpsTrackPoint &point = gpsTrack[gpsTrackNext];
                point.sequence = ++gpsTrackSequence;
                point.capturedAtMs = now;
                point.satellites = satellites;
                point.utcYear = utcYear;
                point.utcMonth = utcMonth;
                point.utcDay = utcDay;
                point.utcHour = utcHour;
                point.utcMinute = utcMinute;
                point.utcSecond = utcSecond;
                point.utcCentisecond = utcCentisecond;
                point.utcValid = utcValid;
                point.latitude = latitude;
                point.longitude = longitude;
                point.altitudeMeters = altitudeMeters;
                point.speedKmph = speedKmph;
                point.hdop = hdop;
                fieldRecord.uptimeMs = now;
                fieldRecord.sequence = point.sequence;
                fieldRecord.satellites = point.satellites;
                fieldRecord.utcYear = point.utcYear;
                fieldRecord.utcMonth = point.utcMonth;
                fieldRecord.utcDay = point.utcDay;
                fieldRecord.utcHour = point.utcHour;
                fieldRecord.utcMinute = point.utcMinute;
                fieldRecord.utcSecond = point.utcSecond;
                fieldRecord.utcCentisecond = point.utcCentisecond;
                fieldRecord.utcValid = point.utcValid;
                fieldRecord.latitude = point.latitude;
                fieldRecord.longitude = point.longitude;
                fieldRecord.altitudeMeters = point.altitudeMeters;
                fieldRecord.speedKmph = point.speedKmph;
                fieldRecord.hdop = point.hdop;
                capturedTrackPoint = true;
                gpsTrackNext = (gpsTrackNext + 1) % kGpsTrackCapacity;
                if (gpsTrackCount < kGpsTrackCapacity) gpsTrackCount++;
                gpsTrackLastCaptureMs = now;
            }
            portEXIT_CRITICAL(&gpsLiveMux);
            if (capturedTrackPoint) heltecFieldLoggerRecordGps(fieldRecord);
            lastSnapshotMs = now;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    serial.end();
    gpsMonitorRunning = false;
    if (!gpsExclusiveUse) setGpsPower(false);
    gpsMonitorTaskHandle = nullptr;
    vTaskDelete(nullptr);
}

uint16_t readBatteryMillivolts() {
    digitalWrite(kBatteryAdcControlPin, HIGH);
    delay(2);
    const uint16_t millivolts = lroundf(analogReadMilliVolts(kBatteryAdcPin) * 4.9f);
    digitalWrite(kBatteryAdcControlPin, LOW);
    return millivolts;
}

void drawLine(uint8_t y, const String &text) {
    oled.drawUTF8(0, y, text.c_str());
}

bool hasOledActionMessage(uint8_t page) {
    return oledActionMessage.length() > 0 && oledActionPage == page && millis() - oledActionMessageMs < 2000;
}

void setOledActionMessage(uint8_t page, const String &message) {
    oledActionPage = page;
    oledActionMessage = message;
    oledActionMessageMs = millis();
}

template <size_t N> constexpr uint8_t itemCount(const OledMenuItem (&)[N]) {
    return static_cast<uint8_t>(N);
}

const OledMenuItem *menuFor(OledScreen screen) {
    switch (screen) {
        case OledScreen::Root: return kRootMenu;
        case OledScreen::Dashboard: return kDashboardMenu;
        case OledScreen::Gps: return kGpsMenu;
        case OledScreen::Lora: return kLoraMenu;
        case OledScreen::FieldLog: return kFieldLogMenu;
        case OledScreen::System: return kSystemMenu;
        case OledScreen::Display: return kDisplayMenu;
        default: return nullptr;
    }
}

uint8_t menuSize(OledScreen screen) {
    switch (screen) {
        case OledScreen::Root: return itemCount(kRootMenu);
        case OledScreen::Dashboard: return itemCount(kDashboardMenu);
        case OledScreen::Gps: return itemCount(kGpsMenu);
        case OledScreen::Lora: return itemCount(kLoraMenu);
        case OledScreen::FieldLog: return itemCount(kFieldLogMenu);
        case OledScreen::System: return itemCount(kSystemMenu);
        case OledScreen::Display: return itemCount(kDisplayMenu);
        default: return 0;
    }
}

const char *menuTitle(OledScreen screen) {
    switch (screen) {
        case OledScreen::Root: return "BRUCE / HELTEC";
        case OledScreen::Dashboard: return "DASHBOARD";
        case OledScreen::Gps: return "GPS MONITOR";
        case OledScreen::Lora: return "LORA RECEIVER";
        case OledScreen::FieldLog: return "FIELD LOGGER";
        case OledScreen::System: return "SYSTEM";
        case OledScreen::Display: return "DISPLAY TIMEOUT";
        default: return "BRUCE";
    }
}

void drawMenuHeader(const char *title) {
    oled.setFont(u8g2_font_6x10_tf);
    String clipped = title ? title : "BRUCE";
    if (clipped.length() > 21) clipped.remove(21);
    oled.drawUTF8(0, 10, clipped.c_str());
    oled.drawHLine(0, 12, 128);
}

void drawMenuLine(uint8_t row, const String &value, bool selected) {
    const uint8_t y = 16 + row * 10;
    String clipped = value;
    if (clipped.length() > 20) clipped.remove(20);
    oled.setFont(u8g2_font_5x8_tf);
    if (selected) {
        oled.drawBox(0, y - 1, 128, 9);
        oled.setDrawColor(0);
        oled.drawUTF8(2, y + 7, (String("> ") + clipped).c_str());
        oled.setDrawColor(1);
    } else {
        oled.drawUTF8(2, y + 7, (String("  ") + clipped).c_str());
    }
}

void renderOledMenu() {
    oled.clearBuffer();
    drawMenuHeader(menuTitle(oledScreen));

    const OledMenuItem *menu = menuFor(oledScreen);
    const uint8_t count = menuSize(oledScreen);
    if (menu && count) {
        uint8_t first = oledSelected > 2 ? oledSelected - 2 : 0;
        if (count > 4 && first > count - 4) first = count - 4;
        for (uint8_t row = 0; row < 4 && first + row < count; row++) {
            drawMenuLine(row, menu[first + row].label, first + row == oledSelected);
        }
    }

    oled.setFont(u8g2_font_4x6_tf);
    if (oledNotice.length() && millis() < oledNoticeUntil) {
        oled.drawUTF8(0, 63, oledNotice.c_str());
    } else {
        oled.drawUTF8(0, 63, "1x next 2x back hold select");
    }
    oled.sendBuffer();
}

void drawWebUiPage() {
    oled.clearBuffer();
    oled.setFont(u8g2_font_6x12_tf);
    const char *title = statusPage == 0   ? "BRUCE WEBUI READY"
                        : statusPage == 1 ? "BRUCE WEBUI LOGIN"
                        : statusPage == 2 ? "HELTEC LIVE GPS"
                        : statusPage == 3 ? "HELTEC LORA RX"
                        : statusPage == 4 ? "HELTEC HARDWARE"
                                          : "HELTEC FIELD LOG";
    drawLine(11, title);
    oled.drawHLine(0, 14, 128);
    if (statusPage == 1) {
        drawLine(29, "User: " + bruceConfig.webUI.user);
        drawLine(41, "Pass: " + bruceConfig.webUI.pwd);
        drawLine(57, "2x: back");
    } else if (statusPage == 2) {
        GpsLiveStatus live;
        portENTER_CRITICAL(&gpsLiveMux);
        live = gpsLiveStatus;
        portEXIT_CRITICAL(&gpsLiveMux);
        const uint32_t now = millis();
        const bool liveFix = live.lastFixMs > 0 && now - live.lastFixMs < 5000;
        const char *state = gpsExclusiveUse       ? "exclusive"
                            : gpsMonitorRunning   ? (gpsMonitorStopRequested ? "stopping" : "running")
                            : gpsMonitorTaskHandle ? "starting"
                                                   : "off";
        drawLine(27, "GPS: " + String(state) + (gpsPowerEnabled ? " / ON" : " / OFF"));
        if (liveFix) {
            drawLine(39, String(live.latitude, 5) + "," + String(live.longitude, 5));
            drawLine(
                51,
                hasOledActionMessage(2) ? oledActionMessage
                                        : "Sat " + String(live.satellites) + " HDOP " + String(live.hdop, 1)
            );
        } else {
            drawLine(39, "NMEA " + String(live.sentences) + "  Sat " + String(live.satellites));
            drawLine(
                51,
                hasOledActionMessage(2) ? oledActionMessage
                                        : (gpsMonitorRunning ? "Waiting for GPS fix" : "Use menu to start GPS")
            );
        }
        drawLine(63, "2x: back");
    } else if (statusPage == 3) {
#if !defined(LITE_VERSION)
        const LoRaRuntimeSnapshot lora = loraRuntimeSnapshot();
        const float frequencyMHz =
            lora.frequencyMHz > 0.0f ? lora.frequencyMHz : kPassiveRadioFrequencyMHz;
        drawLine(
            29,
            String(lora.listening ? "RX " : "OFF ") + String(frequencyMHz, 3) + " P:" +
                String(lora.packetsReceived)
        );
        drawLine(
            41,
            lora.hasPacket
                ? "RSSI " + String(lora.lastRssiDbm, 0) + " SNR " + String(lora.lastSnrDb, 1)
                : (lora.listening ? "Waiting for packets" : "Use menu to start RX")
        );
        drawLine(
            53,
            hasOledActionMessage(3)
                ? oledActionMessage
                : (lora.hasPacket ? String(lora.lastMessage).substring(0, 21)
                                  : "Radio code " + String(lora.lastState))
        );
#else
        drawLine(35, "LoRa unavailable");
#endif
        drawLine(63, "2x: back");
    } else if (statusPage == 4) {
        drawLine(
            29,
            radioReceiveDiagnostic == RADIOLIB_ERR_NONE ? "SX1262 RX: OK"
                                                        : "SX1262: ERR " + String(radioReceiveDiagnostic)
        );
        if (gpsDiagnosticFix) drawLine(41, "GPS FIX, sat " + String(gpsDiagnosticSatellites));
        else if (gpsDiagnosticSentences > 0) drawLine(41, "GPS NMEA: " + String(gpsDiagnosticSentences));
        else drawLine(41, "GPS: no NMEA data");
        const uint16_t batteryMv = readBatteryMillivolts();
        drawLine(
            53,
            hasOledActionMessage(4)
                ? oledActionMessage
                : (batteryMv > 2500 ? "Battery: " + String(batteryMv) + "mV" : "Battery: not found")
        );
        drawLine(63, "2x: back");
    } else if (statusPage == 5) {
        const HeltecFieldLogSnapshot fieldLog = heltecFieldLoggerSnapshot();
        drawLine(
            27,
            String("Log: ") + (fieldLog.active ? "ACTIVE" : "off") +
                (fieldLog.autoResume ? " Auto:on" : " Auto:off")
        );
        drawLine(
            39,
            "G" + String(fieldLog.gpsFixes) + " B" + String(fieldLog.bleObservations) +
                " W" + String(fieldLog.wifiObservations)
        );
        drawLine(
            51,
            hasOledActionMessage(5)
                ? oledActionMessage
                : "S" + String(fieldLog.sessionId) + " " + String(fieldLog.sessionBytes / 1024) + "KiB"
        );
        drawLine(63, "2x: back");
    } else if (webUiApMode) {
        drawLine(29, "WiFi: " + bruceConfig.wifiAp.ssid);
        drawLine(41, "Pass: " + bruceConfig.wifiAp.pwd);
        drawLine(53, WiFi.softAPIP().toString());
        drawLine(63, "2x: back");
    } else {
        drawLine(29, "Connected to WiFi");
        drawLine(41, WiFi.localIP().toString());
        drawLine(53, "http://bruce.local");
        drawLine(63, "2x: back");
    }
    oled.sendBuffer();
}

void renderOledConfirm() {
    oled.clearBuffer();
    drawMenuHeader(oledPendingAction == kPowerDown ? "POWER DOWN" : "SLEEP");
    oled.setFont(u8g2_font_5x8_tf);
    oled.drawUTF8(0, 25, oledPendingLabel ? oledPendingLabel : "Power action");
    oled.drawUTF8(
        0,
        37,
        oledPendingAction == kSleep ? "PRG wakes + restarts" : "RST/power cycle wakes"
    );
    oled.drawUTF8(0, 49, "Hold: confirm");
    oled.drawUTF8(0, 61, "1x / 2x: cancel");
    oled.sendBuffer();
}

void renderOledDeviceInfo() {
    oled.clearBuffer();
    drawMenuHeader("DEVICE INFO");
    oled.setFont(u8g2_font_5x8_tf);
    oled.drawUTF8(0, 24, "Heltec WiFi LoRa V4");
    String line = String("Bruce ") + BRUCE_VERSION;
    if (line.length() > 24) line.remove(24);
    oled.drawUTF8(0, 35, line.c_str());
    const uint16_t batteryMv = readBatteryMillivolts();
    line = batteryMv > 2500 ? "Battery " + String(batteryMv) + " mV" : "Battery: USB/no cell";
    oled.drawUTF8(0, 46, line.c_str());
    line = webUiApMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
    oled.drawUTF8(0, 57, line.c_str());
    oled.sendBuffer();
}

void renderOledHelp() {
    oled.clearBuffer();
    drawMenuHeader("PRG BUTTON");
    oled.setFont(u8g2_font_5x8_tf);
    oled.drawUTF8(0, 23, "1x      next item");
    oled.drawUTF8(0, 33, "2x      back / stop");
    oled.drawUTF8(0, 43, "Long    select / start");
    oled.drawUTF8(0, 53, "Long press = 0.9 sec");
    oled.drawUTF8(0, 63, "First press wakes screen");
    oled.sendBuffer();
}

void renderOledUi() {
    if (!oledUiReady || oledScreen == OledScreen::Boot || oledScreenBlanked) return;
    switch (oledScreen) {
        case OledScreen::Status: drawWebUiPage(); break;
        case OledScreen::Confirm: renderOledConfirm(); break;
        case OledScreen::DeviceInfo: renderOledDeviceInfo(); break;
        case OledScreen::Help: renderOledHelp(); break;
        default: renderOledMenu(); break;
    }
}

void initializeStandaloneOledUi() {
    const bool supportedTimeout =
        bruceConfig.dimmerSet == 0 || bruceConfig.dimmerSet == 15 || bruceConfig.dimmerSet == 30 ||
        bruceConfig.dimmerSet == 45 || bruceConfig.dimmerSet == 60;
    oledScreenTimeoutSeconds = supportedTimeout ? static_cast<uint8_t>(bruceConfig.dimmerSet) : 60;
    if (!supportedTimeout) bruceConfig.setDimmer(oledScreenTimeoutSeconds);

    oledUiReady = true;
    oledScreen = OledScreen::Root;
    oledReturnScreen = OledScreen::Root;
    oledSelected = 0;
    oledNotice = "Ready";
    oledNoticeUntil = millis() + 1200;
    oledLastInteraction = millis();
    oledScreenBlanked = false;
    oled.setPowerSave(0);
    renderOledUi();

    if (
        !oledInputTaskHandle &&
        xTaskCreate(oledInputTask, "HeltecOledInput", 4096, nullptr, 3, &oledInputTaskHandle) != pdPASS
    ) {
        oledInputTaskHandle = nullptr;
        Serial.println("[HELTEC] Could not start dedicated OLED input task");
    }
}
}

void heltecV4BeginStandaloneMenu() {
    webUiApMode = true;
    initializeStandaloneOledUi();
}

void heltecV4DrawWebUiStatus(bool apMode) {
    webUiApMode = apMode;
    if (!oledUiReady) initializeStandaloneOledUi();
    oledNotice = "WebUI ready";
    oledNoticeUntil = millis() + 1200;
    renderOledUi();
}

void heltecV4DrawBootStage(const char *stage) {
    oled.setDrawColor(0);
    oled.drawBox(0, 52, 128, 12);
    oled.setDrawColor(1);
    oled.setFont(u8g2_font_5x8_tf);
    oled.drawUTF8(0, 61, stage);
    oled.sendBuffer();
}

void _setup_gpio() {
    // Vext powers the board peripherals and is active-low on the standard V4.
    pinMode(kVextPin, OUTPUT);
    digitalWrite(kVextPin, LOW);
    pinMode(kButtonPin, INPUT_PULLUP);
    oledRawPressed = digitalRead(kButtonPin) == LOW;
    oledStablePressed = oledRawPressed;
    oledRawChangedAt = millis();
    oledLastInteraction = oledRawChangedAt;
    oledScreen = OledScreen::Boot;
    pinMode(kBatteryAdcControlPin, OUTPUT);
    digitalWrite(kBatteryAdcControlPin, LOW);
    setGpsPower(false);

    // Keep the V4 RF front end disabled and in receive direction until the
    // SX1262 is explicitly initialized.
    pinMode(LORA_FEM_POWER, OUTPUT);
    pinMode(LORA_FEM_ENABLE, OUTPUT);
    pinMode(LORA_FEM_TX, OUTPUT);
    digitalWrite(LORA_FEM_POWER, LOW);
    digitalWrite(LORA_FEM_ENABLE, LOW);
    digitalWrite(LORA_FEM_TX, LOW);

    delay(50);
    Wire.begin(kOledDataPin, kOledClockPin);
    oled.setI2CAddress(0x3C << 1);
    oled.setBusClock(400000);
    oled.begin();
    oled.setFontMode(1);
    oled.clearBuffer();
    oled.setFont(u8g2_font_6x12_tf);
    drawLine(13, "BRUCE / HELTEC V4");
    oled.drawHLine(0, 16, 128);
    drawLine(34, "Starting firmware...");
    drawLine(50, "Standalone + WebUI");
    oled.sendBuffer();

    // Bruce's graphical UI requires a color TFT. Start the supported headless
    // interface until an SSD1306 display adapter is available.
    bruceConfig.startupApp = "WebUI";
}

void _post_setup_gpio() {
    // Migrate the provisional pin pair used by the first Heltec V4 build to
    // the board's dedicated GNSS connector pins from the official schematic.
    if (bruceConfigPins.gps_bus.rx == GPIO_NUM_33 && bruceConfigPins.gps_bus.tx == GPIO_NUM_34) {
        bruceConfigPins.gps_bus.rx = GPIO_NUM_39;
        bruceConfigPins.gps_bus.tx = GPIO_NUM_38;
        bruceConfigPins.saveFile();
    }

    // Arduino installs the shared GPIO ISR service lazily on the first
    // attachInterrupt(). Doing that from an AsyncTCP WebUI callback forces the
    // allocation through the small cross-core IPC task and can trip its debug
    // exception/stack guard. Install it once from the controlled boot task;
    // Arduino treats ESP_ERR_INVALID_STATE as an already initialized service.
    const esp_err_t gpioIsrStatus = gpio_install_isr_service(0);
    if (gpioIsrStatus != ESP_OK && gpioIsrStatus != ESP_ERR_INVALID_STATE) {
        Serial.printf("[HELTEC] GPIO ISR service initialization failed: %d\n", gpioIsrStatus);
    }

    heltecV4DrawBootStage("Testing SX1262");
    digitalWrite(LORA_FEM_POWER, HIGH);
    digitalWrite(LORA_FEM_ENABLE, HIGH);
    digitalWrite(LORA_FEM_TX, LOW);
    delay(5);
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
    Module radioModule(LORA_CS, LORA_IRQ, LORA_RST, LORA_BUSY, SPI);
    SX1262 radio(&radioModule);
    // Initialization is receive-side only; this does not transmit a packet.
    radioDiagnostic = radio.begin(kPassiveRadioFrequencyMHz);
    if (radioDiagnostic == RADIOLIB_ERR_NONE) {
        radioReceiveDiagnostic = radio.startReceive();
        delay(20);
        radio.sleep();
    }
    SPI.end();
    digitalWrite(LORA_FEM_ENABLE, LOW);
    digitalWrite(LORA_FEM_POWER, LOW);
    Serial.printf(
        "[HELTEC] SX1262 passive self-test: init=%d receive=%d\n",
        radioDiagnostic,
        radioReceiveDiagnostic
    );

    heltecV4DrawBootStage("Checking GPS NMEA");
    setGpsPower(true);
    delay(100);
    HardwareSerial gpsDiagnosticSerial(2);
    TinyGPSPlus gpsParser;
    gpsDiagnosticSerial.begin(
        bruceConfigPins.gpsBaudrate,
        SERIAL_8N1,
        bruceConfigPins.gps_bus.rx,
        bruceConfigPins.gps_bus.tx
    );
    String sentence;
    const uint32_t started = millis();
    while (millis() - started < 2500) {
        while (gpsDiagnosticSerial.available()) {
            const char c = (char)gpsDiagnosticSerial.read();
            gpsDiagnosticBytes++;
            gpsParser.encode(c);
            if (c == '\n') {
                sentence.trim();
                if (sentence.startsWith("$G") && sentence.indexOf('*') > 0) gpsDiagnosticSentences++;
                sentence = "";
            } else if (c != '\r' && sentence.length() < 100) {
                sentence += c;
            }
        }
        delay(5);
    }
    gpsDiagnosticSerial.end();
    gpsDiagnosticFix = gpsParser.location.isValid() && gpsParser.location.age() < 5000;
    if (gpsParser.satellites.isValid()) gpsDiagnosticSatellites = gpsParser.satellites.value();
    setGpsPower(false);
    Serial.printf(
        "[HELTEC] GPS passive self-test: %u bytes, %u NMEA sentences, fix=%s, satellites=%u\n",
        (unsigned)gpsDiagnosticBytes,
        (unsigned)gpsDiagnosticSentences,
        gpsDiagnosticFix ? "yes" : "no",
        (unsigned)gpsDiagnosticSatellites
    );
}

int getBattery() {
    const float millivolts = readBatteryMillivolts();
    int percent = lroundf((millivolts - 3300.0f) * 100.0f / (4200.0f - 3300.0f));
    return constrain(percent, 1, 100);
}

String heltecV4HardwareStatusJson() {
    JsonDocument doc;
    const uint32_t now = millis();
    doc["board"] = "Heltec WiFi LoRa 32 V4";
    doc["firmware"]["version"] = BRUCE_VERSION;
    doc["firmware"]["commit"] = GIT_COMMIT_HASH;
    doc["system"]["uptimeMs"] = now;
    doc["system"]["heap"]["totalBytes"] = ESP.getHeapSize();
    doc["system"]["heap"]["freeBytes"] = ESP.getFreeHeap();
    doc["system"]["heap"]["minimumFreeBytes"] = ESP.getMinFreeHeap();
    doc["system"]["heap"]["maximumAllocationBytes"] = ESP.getMaxAllocHeap();
    const bool hasPsram = psramFound();
    doc["system"]["psram"]["present"] = hasPsram;
    doc["system"]["psram"]["totalBytes"] = hasPsram ? ESP.getPsramSize() : 0;
    doc["system"]["psram"]["freeBytes"] = hasPsram ? ESP.getFreePsram() : 0;
    const wifi_mode_t wifiMode = WiFi.getMode();
    const bool apActive = wifiMode == WIFI_MODE_AP || wifiMode == WIFI_MODE_APSTA;
    doc["network"]["mode"] = wifiMode == WIFI_MODE_APSTA  ? "AP+STA"
                               : wifiMode == WIFI_MODE_AP ? "AP"
                               : wifiMode == WIFI_MODE_STA ? "STA"
                                                           : "off";
    doc["network"]["apActive"] = apActive;
    if (apActive) {
        doc["network"]["ssid"] = WiFi.softAPSSID();
        doc["network"]["ip"] = WiFi.softAPIP().toString();
        doc["network"]["mac"] = WiFi.softAPmacAddress();
        doc["network"]["channel"] = WiFi.channel();
        doc["network"]["connectedClients"] = WiFi.softAPgetStationNum();
    }
    doc["sx1262"]["initCode"] = radioDiagnostic;
    doc["sx1262"]["receiveCode"] = radioReceiveDiagnostic;
    doc["sx1262"]["diagnosticFrequencyMHz"] = kPassiveRadioFrequencyMHz;
    doc["sx1262"]["ok"] =
        radioDiagnostic == RADIOLIB_ERR_NONE && radioReceiveDiagnostic == RADIOLIB_ERR_NONE;
    doc["gps"]["rxPin"] = (int)bruceConfigPins.gps_bus.rx;
    doc["gps"]["txPin"] = (int)bruceConfigPins.gps_bus.tx;
    doc["gps"]["baud"] = bruceConfigPins.gpsBaudrate;
    doc["gps"]["bytes"] = gpsDiagnosticBytes;
    doc["gps"]["nmeaSentences"] = gpsDiagnosticSentences;
    doc["gps"]["fix"] = gpsDiagnosticFix;
    doc["gps"]["satellites"] = gpsDiagnosticSatellites;
    GpsLiveStatus live;
    size_t trackCount = 0;
    portENTER_CRITICAL(&gpsLiveMux);
    live = gpsLiveStatus;
    trackCount = gpsTrackCount;
    portEXIT_CRITICAL(&gpsLiveMux);
    const bool liveFix = live.lastFixMs > 0 && now - live.lastFixMs < 5000;
    const char *monitorState = gpsExclusiveUse       ? "exclusive"
                               : gpsMonitorRunning   ? (gpsMonitorStopRequested ? "stopping" : "running")
                               : gpsMonitorTaskHandle ? "starting"
                                                      : "off";
    doc["gps"]["monitorState"] = monitorState;
    doc["gps"]["powered"] = (bool)gpsPowerEnabled;
    doc["gps"]["live"]["bytes"] = live.bytes;
    doc["gps"]["live"]["sentences"] = live.sentences;
    doc["gps"]["live"]["fix"] = liveFix;
    doc["gps"]["live"]["satellites"] = live.satellites;
    doc["gps"]["live"]["lastDataAgeMs"] = live.lastDataMs > 0 ? now - live.lastDataMs : 0;
    doc["gps"]["track"]["count"] = trackCount;
    doc["gps"]["track"]["capacity"] = kGpsTrackCapacity;
    doc["gps"]["track"]["minimumIntervalMs"] = kGpsTrackMinimumIntervalMs;
#if !defined(LITE_VERSION)
    doc["ble"]["apiEnabled"] = isBLEAPIEnabled();
    doc["ble"]["advertising"] = bleApiAdvertising();
    doc["ble"]["connectedClients"] = bleApiConnectedClients();
    doc["ble"]["connectionCount"] = bleApiConnectionCount();
#endif
    if (live.lastFixMs > 0) {
        doc["gps"]["live"]["latitude"] = live.latitude;
        doc["gps"]["live"]["longitude"] = live.longitude;
        doc["gps"]["live"]["altitudeMeters"] = live.altitudeMeters;
        doc["gps"]["live"]["speedKmph"] = live.speedKmph;
        doc["gps"]["live"]["hdop"] = live.hdop;
        doc["gps"]["live"]["fixAgeMs"] = now - live.lastFixMs;
    }
    const uint16_t batteryMv = readBatteryMillivolts();
    doc["battery"]["millivolts"] = batteryMv;
    doc["battery"]["present"] = batteryMv > 2500;
    doc["battery"]["percent"] = batteryMv > 2500 ? getBattery() : 0;
    String output;
    serializeJson(doc, output);
    return output;
}

String heltecV4GpsTrackJson() {
    GpsTrackPoint *snapshot = static_cast<GpsTrackPoint *>(malloc(sizeof(gpsTrack)));
    size_t count = 0;
    size_t next = 0;
    portENTER_CRITICAL(&gpsLiveMux);
    count = gpsTrackCount;
    next = gpsTrackNext;
    const size_t oldest = (next + kGpsTrackCapacity - count) % kGpsTrackCapacity;
    if (snapshot) {
        for (size_t offset = 0; offset < count; offset++) {
            snapshot[offset] = gpsTrack[(oldest + offset) % kGpsTrackCapacity];
        }
    }
    portEXIT_CRITICAL(&gpsLiveMux);

    const uint32_t now = millis();
    JsonDocument doc;
    doc["capacity"] = kGpsTrackCapacity;
    doc["count"] = count;
    doc["minimumIntervalMs"] = kGpsTrackMinimumIntervalMs;
    doc["uptimeMs"] = now;
    doc["recording"] = gpsMonitorRunning && !gpsMonitorStopRequested;
    JsonArray points = doc["points"].to<JsonArray>();
    for (size_t offset = 0; offset < count; offset++) {
        GpsTrackPoint point;
        if (snapshot) {
            point = snapshot[offset];
        } else {
            portENTER_CRITICAL(&gpsLiveMux);
            point = gpsTrack[(oldest + offset) % kGpsTrackCapacity];
            portEXIT_CRITICAL(&gpsLiveMux);
        }

        JsonObject jsonPoint = points.add<JsonObject>();
        jsonPoint["sequence"] = point.sequence;
        jsonPoint["capturedAtMs"] = point.capturedAtMs;
        jsonPoint["ageMs"] = now - point.capturedAtMs;
        jsonPoint["latitude"] = point.latitude;
        jsonPoint["longitude"] = point.longitude;
        jsonPoint["satellites"] = point.satellites;
        if (point.utcValid) {
            char utc[24];
            snprintf(
                utc,
                sizeof(utc),
                "%04u-%02u-%02uT%02u:%02u:%02u.%02uZ",
                static_cast<unsigned>(point.utcYear),
                static_cast<unsigned>(point.utcMonth),
                static_cast<unsigned>(point.utcDay),
                static_cast<unsigned>(point.utcHour),
                static_cast<unsigned>(point.utcMinute),
                static_cast<unsigned>(point.utcSecond),
                static_cast<unsigned>(point.utcCentisecond)
            );
            jsonPoint["utc"] = utc;
        }
        if (!isnan(point.altitudeMeters)) jsonPoint["altitudeMeters"] = point.altitudeMeters;
        if (!isnan(point.speedKmph)) jsonPoint["speedKmph"] = point.speedKmph;
        if (!isnan(point.hdop)) jsonPoint["hdop"] = point.hdop;
    }

    String output;
    serializeJson(doc, output);
    free(snapshot);
    return output;
}

void heltecV4ClearGpsTrack() {
    portENTER_CRITICAL(&gpsLiveMux);
    memset(gpsTrack, 0, sizeof(gpsTrack));
    gpsTrackCount = 0;
    gpsTrackNext = 0;
    gpsTrackSequence = 0;
    gpsTrackLastCaptureMs = 0;
    portEXIT_CRITICAL(&gpsLiveMux);
}

bool heltecV4SetGpsMonitor(bool enabled) {
    if (!enabled) {
        gpsMonitorStopRequested = true;
        if (!gpsMonitorTaskHandle && !gpsExclusiveUse) setGpsPower(false);
        return true;
    }
    if (gpsExclusiveUse) return false;
    if (gpsMonitorTaskHandle) return !gpsMonitorStopRequested;

    resetGpsLiveStatus();
    gpsMonitorStopRequested = false;
    if (xTaskCreate(gpsMonitorTask, "HeltecGpsMonitor", 4096, nullptr, 1, &gpsMonitorTaskHandle) != pdPASS) {
        gpsMonitorTaskHandle = nullptr;
        setGpsPower(false);
        return false;
    }
    return true;
}

bool heltecV4GpsMonitorActive() {
    return gpsMonitorTaskHandle != nullptr && !gpsMonitorStopRequested;
}

void heltecV4PrepareGpsForExclusiveUse() {
    gpsExclusiveUse = true;
    gpsMonitorStopRequested = true;
    const uint32_t stopStartedMs = millis();
    while (gpsMonitorTaskHandle && millis() - stopStartedMs < 1000) vTaskDelay(pdMS_TO_TICKS(10));
    setGpsPower(true);
}

void heltecV4ReleaseGpsExclusiveUse() {
    gpsExclusiveUse = false;
    setGpsPower(false);
}

bool isCharging() { return false; }

void _setBrightness(uint8_t brightval) { (void)brightval; }

namespace {
void showOledStatus(uint8_t page, OledScreen returnScreen) {
    statusPage = page;
    oledReturnScreen = returnScreen;
    oledScreen = OledScreen::Status;
    oledSelected = 0;
}

void setOledTimeout(uint8_t seconds) {
    oledScreenTimeoutSeconds = seconds;
    bruceConfig.setDimmer(seconds);
    oledLastInteraction = millis();
    previousMillis = oledLastInteraction;
    oledNotice = seconds == 0 ? "Screen always on" : "Screen off after " + String(seconds) + "s";
    oledNoticeUntil = oledLastInteraction + 1800;
    oledSelected = 0;
}

void toggleGpsFromOled() {
    bool ok = false;
    String message;
    const bool enable = gpsMonitorTaskHandle == nullptr;
    if (gpsExclusiveUse) {
        message = "GPS busy";
    } else if (gpsMonitorTaskHandle && gpsMonitorStopRequested) {
        message = "GPS stopping";
    } else if (gpsMonitorTaskHandle && heltecFieldLoggerUsesGps()) {
        message = "Field log owns GPS";
    } else {
        ok = heltecV4SetGpsMonitor(enable);
        message = ok ? (enable ? "GPS starting" : "GPS stopping") : "GPS action failed";
        Serial.printf(
            "[HELTEC] OLED GPS monitor %s: %s\n",
            enable ? "start" : "stop",
            ok ? "ok" : "failed"
        );
    }
    setOledActionMessage(2, message);
    showOledStatus(2, OledScreen::Gps);
}

void toggleLoraFromOled() {
#if !defined(LITE_VERSION)
    const LoRaRuntimeSnapshot before = loraRuntimeSnapshot();
    const float frequencyMHz =
        before.frequencyMHz > 0.0f ? before.frequencyMHz : kPassiveRadioFrequencyMHz;
    const bool ok = heltecV4ToggleLoraReceiver(frequencyMHz);
    const LoRaRuntimeSnapshot after = loraRuntimeSnapshot();
    setOledActionMessage(
        3,
        ok ? (after.listening ? "LoRa RX started" : "LoRa RX stopped") : "LoRa action failed"
    );
    Serial.printf(
        "[HELTEC] OLED LoRa RX %s at %.3f MHz: %s\n",
        before.listening ? "stop" : "start",
        frequencyMHz,
        ok ? "ok" : "failed"
    );
#else
    setOledActionMessage(3, "LoRa unavailable");
#endif
    showOledStatus(3, OledScreen::Lora);
}

void setFieldLogFromOled(bool enable) {
    const bool wasActive = heltecFieldLoggerIsActive();
    const bool ok = enable ? (wasActive || heltecFieldLoggerStart(true, true, true, true))
                           : (!wasActive || heltecFieldLoggerStop());
    setOledActionMessage(
        5,
        ok ? (enable ? (wasActive ? "Log already active" : "Log started")
                     : (wasActive ? "Log stopped" : "Log already stopped"))
           : "Log action failed"
    );
    Serial.printf(
        "[HELTEC] OLED field logger %s: %s\n",
        enable ? "start" : "stop",
        ok ? "ok" : "failed"
    );
    showOledStatus(5, OledScreen::FieldLog);
}

void goBackOled() {
    switch (oledScreen) {
        case OledScreen::Status:
        case OledScreen::Confirm: oledScreen = oledReturnScreen; break;
        case OledScreen::DeviceInfo:
        case OledScreen::Help:
        case OledScreen::Display: oledScreen = OledScreen::System; break;
        case OledScreen::Dashboard:
        case OledScreen::Gps:
        case OledScreen::Lora:
        case OledScreen::FieldLog:
        case OledScreen::System: oledScreen = OledScreen::Root; break;
        default: break;
    }
    oledSelected = 0;
}

void selectOledItem() {
    if (oledScreen == OledScreen::Status || oledScreen == OledScreen::DeviceInfo ||
        oledScreen == OledScreen::Help) {
        goBackOled();
        return;
    }
    if (oledScreen == OledScreen::Confirm) {
        if (oledPendingAction == kSleep) enterHeltecDeepSleep(true);
        if (oledPendingAction == kPowerDown) enterHeltecDeepSleep(false);
        return;
    }

    const OledMenuItem *menu = menuFor(oledScreen);
    const uint8_t count = menuSize(oledScreen);
    if (!menu || oledSelected >= count) return;
    const OledMenuItem &item = menu[oledSelected];

    if (item.requiresConfirmation) {
        oledPendingAction = item.action;
        oledPendingLabel = item.label;
        oledReturnScreen = oledScreen;
        oledScreen = OledScreen::Confirm;
        return;
    }

    switch (item.action) {
        case kOpenDashboard: oledScreen = OledScreen::Dashboard; break;
        case kOpenGps: oledScreen = OledScreen::Gps; break;
        case kOpenLora: oledScreen = OledScreen::Lora; break;
        case kOpenFieldLog: oledScreen = OledScreen::FieldLog; break;
        case kOpenSystem: oledScreen = OledScreen::System; break;
        case kOpenDisplay: oledScreen = OledScreen::Display; break;
        case kShowNetwork: showOledStatus(0, oledScreen); return;
        case kShowLogin: showOledStatus(1, oledScreen); return;
        case kShowGps: showOledStatus(2, oledScreen); return;
        case kShowLora: showOledStatus(3, oledScreen); return;
        case kShowHardware: showOledStatus(4, oledScreen); return;
        case kShowFieldLog: showOledStatus(5, oledScreen); return;
        case kShowDeviceInfo:
            oledReturnScreen = OledScreen::System;
            oledScreen = OledScreen::DeviceInfo;
            return;
        case kShowHelp:
            oledReturnScreen = OledScreen::System;
            oledScreen = OledScreen::Help;
            return;
        case kToggleGps: toggleGpsFromOled(); return;
        case kToggleLora: toggleLoraFromOled(); return;
        case kStartFieldLog: setFieldLogFromOled(true); return;
        case kStopFieldLog: setFieldLogFromOled(false); return;
        case kTimeoutOff: setOledTimeout(0); return;
        case kTimeout15: setOledTimeout(15); return;
        case kTimeout30: setOledTimeout(30); return;
        case kTimeout45: setOledTimeout(45); return;
        case kTimeout60: setOledTimeout(60); return;
        case kBack: goBackOled(); return;
        default: return;
    }
    oledSelected = 0;
}

void dispatchOledGesture(OledGesture gesture) {
    oledLastInteraction = millis();
    previousMillis = oledLastInteraction;
    switch (gesture) {
        case OledGesture::Single: {
            if (oledScreen == OledScreen::Confirm) {
                goBackOled();
            } else if (
                oledScreen != OledScreen::Status && oledScreen != OledScreen::DeviceInfo &&
                oledScreen != OledScreen::Help
            ) {
                const uint8_t count = menuSize(oledScreen);
                if (count) oledSelected = (oledSelected + 1) % count;
            }
            break;
        }
        case OledGesture::Double: goBackOled(); break;
        case OledGesture::Long: selectOledItem(); break;
    }
    renderOledUi();
    oledLastInteraction = millis();
    previousMillis = oledLastInteraction;
}

void wakeOledScreen(uint32_t now) {
    oledScreenBlanked = false;
    isScreenOff = false;
    dimmer = false;
    oledLastInteraction = now;
    previousMillis = now;
    oled.setPowerSave(0);
    renderOledUi();
}

void pollOledButton(uint32_t now) {
    const bool pressed = digitalRead(kButtonPin) == LOW;
    if (pressed != oledRawPressed) {
        oledRawPressed = pressed;
        oledRawChangedAt = now;
        oledLastInteraction = now;
        previousMillis = now;
    }

    if (now - oledRawChangedAt >= kDebounceMs && oledStablePressed != oledRawPressed) {
        oledStablePressed = oledRawPressed;
        if (oledStablePressed) {
            oledLastInteraction = now;
            if (oledScreenBlanked) {
                oledWakePress = true;
                oledLongDispatched = true;
                oledClickCount = 0;
                wakeOledScreen(now);
                return;
            }
            oledPressedAt = now;
            oledLongDispatched = false;
        } else {
            if (oledWakePress) {
                oledWakePress = false;
                oledLongDispatched = false;
                oledClickCount = 0;
                return;
            }
            const uint32_t duration = now - oledPressedAt;
            if (!oledLongDispatched) {
                if (duration >= kLongPressMs) {
                    oledClickCount = 0;
                    dispatchOledGesture(OledGesture::Long);
                } else {
                    if (oledClickCount < 2) oledClickCount++;
                    oledClickDeadline = now + kClickWindowMs;
                }
            }
        }
    }

    if (
        oledStablePressed && !oledWakePress && !oledLongDispatched &&
        now - oledPressedAt >= kLongPressMs
    ) {
        oledLongDispatched = true;
        oledClickCount = 0;
        dispatchOledGesture(OledGesture::Long);
    }

    if (
        !oledRawPressed && !oledStablePressed && oledClickCount > 0 &&
        now >= oledClickDeadline
    ) {
        const uint8_t completed = oledClickCount;
        oledClickCount = 0;
        dispatchOledGesture(completed == 1 ? OledGesture::Single : OledGesture::Double);
    }
}

void serviceOledUi(uint32_t now) {
    pollOledButton(now);
    now = millis();

    const bool interactionInProgress =
        oledRawPressed || oledStablePressed || oledClickCount > 0;
    if (
        !oledScreenBlanked && oledScreenTimeoutSeconds > 0 && !interactionInProgress &&
        now - oledLastInteraction >= static_cast<uint32_t>(oledScreenTimeoutSeconds) * 1000UL
    ) {
        oled.setPowerSave(1);
        oledScreenBlanked = true;
        isScreenOff = true;
        dimmer = false;
    }
    if (oledScreenBlanked) return;

    if (oledScreen == OledScreen::Status && now - oledLastRender >= 1000) {
        oledLastRender = now;
        renderOledUi();
    } else if (oledNotice.length() && now >= oledNoticeUntil) {
        oledNotice = "";
        oledLastRender = now;
        renderOledUi();
    }
}

void oledInputTask(void *parameter) {
    (void)parameter;
    while (true) {
        if (oledUiReady) serviceOledUi(millis());
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

[[noreturn]] void enterHeltecDeepSleep(bool wakeOnButton) {
    heltecFieldLoggerSuspendForSleep();
    oled.clearBuffer();
    drawMenuHeader(wakeOnButton ? "SLEEP" : "POWER DOWN");
    oled.setFont(u8g2_font_5x8_tf);
    oled.drawUTF8(0, 28, wakeOnButton ? "Press PRG to wake" : "Use RST or power cycle");
    oled.drawUTF8(0, 42, "Stopping radios + GPS");
    oled.drawUTF8(0, 56, "Entering deep sleep...");
    oled.sendBuffer();

#if !defined(LITE_VERSION)
    const LoRaRuntimeSnapshot lora = loraRuntimeSnapshot();
    if (lora.listening) {
        heltecV4ToggleLoraReceiver(lora.frequencyMHz);
    }
#endif
    gpsMonitorStopRequested = true;
    setGpsPower(false);
    digitalWrite(kBatteryAdcControlPin, LOW);
    digitalWrite(LORA_FEM_TX, LOW);
    digitalWrite(LORA_FEM_ENABLE, LOW);
    digitalWrite(LORA_FEM_POWER, LOW);

    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    while (digitalRead(kButtonPin) == LOW) delay(10);
    if (wakeOnButton) esp_sleep_enable_ext0_wakeup(kButtonPin, LOW);

    Serial.println(
        wakeOnButton ? "[HELTEC] Sleeping; PRG wakes the device"
                     : "[HELTEC] Powered down; RST or power cycle required"
    );
    Serial.flush();
    delay(250);
    oled.setPowerSave(1);
    digitalWrite(kVextPin, HIGH);
    esp_deep_sleep_start();
    __builtin_unreachable();
}
} // namespace

void InputHandler(void) {
    heltecFieldLoggerPoll();
    PrevPress = false;
    NextPress = false;
    SelPress = false;
    AnyKeyPress = false;
    EscPress = false;

#if !defined(LITE_VERSION)
    if (oledUiReady) {
        heltecV4PollLoraReceiver();
    }
#endif
}

void powerOff() {
    enterHeltecDeepSleep(true);
}

void checkReboot() {}
