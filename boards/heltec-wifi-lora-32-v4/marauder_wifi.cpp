// Passive Wi-Fi survey service for the unified Heltec firmware.
//
// The bounded device-list and wardriving behavior is adapted from ESP32
// Marauder (Copyright (c) 2020 Just Call Me Koko, MIT License). Bruce remains
// the sole owner of the WebUI, settings, display, GPS, BLE and filesystem
// lifecycles so those subsystems are not duplicated here.

#include "marauder_wifi.h"

#include "field_logger.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp32-hal-psram.h>
#include <esp_wifi_types.h>

namespace {
constexpr size_t kResultCapacity = 64;
constexpr uint32_t kLoggerScanIntervalMs = 15000;
constexpr uint32_t kInitialAutoScanDelayMs = 30000;
constexpr uint32_t kLoggerStateRefreshMs = 500;
constexpr uint32_t kScanTimeoutMs = 20000;
constexpr uint32_t kPassiveDwellMs = 90;

struct SurveyNetwork {
    char bssid[18] = {};
    char ssid[33] = {};
    char authentication[24] = {};
    int32_t rssiDbm = -127;
    uint8_t channel = 0;
    bool hidden = false;
};

SemaphoreHandle_t surveyMutex = nullptr;
bool initialized = false;
bool scanning = false;
bool manualScanPending = false;
bool fieldLoggerEnabled = false;
uint32_t generation = 0;
uint32_t scansCompleted = 0;
uint32_t scanStartedMs = 0;
uint32_t serviceStartedMs = 0;
uint32_t lastScanCompletedMs = 0;
uint32_t lastScanDurationMs = 0;
uint32_t lastLoggerStateRefreshMs = 0;
uint16_t networkCount = 0;
uint16_t droppedNetworks = 0;
wifi_mode_t modeBeforeScan = WIFI_MODE_NULL;
String lastError;
SurveyNetwork *networks = nullptr;
SurveyNetwork *captureNetworks = nullptr;

class SurveyLock {
public:
    explicit SurveyLock(TickType_t wait = pdMS_TO_TICKS(250)) {
        locked = surveyMutex && xSemaphoreTake(surveyMutex, wait) == pdTRUE;
    }
    ~SurveyLock() {
        if (locked) xSemaphoreGive(surveyMutex);
    }
    explicit operator bool() const { return locked; }

private:
    bool locked = false;
};

const char *authenticationName(wifi_auth_mode_t mode) {
    // Keep the stable labels used by Marauder wardrive output while sharing
    // Bruce's Wi-Fi stack and connection lifecycle.
    switch (mode) {
        case WIFI_AUTH_OPEN: return "OPEN";
        case WIFI_AUTH_WEP: return "WEP";
        case WIFI_AUTH_WPA_PSK: return "WPA_PSK";
        case WIFI_AUTH_WPA2_PSK: return "WPA2_PSK";
        case WIFI_AUTH_WPA_WPA2_PSK: return "WPA_WPA2_PSK";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2_ENTERPRISE";
        case WIFI_AUTH_WPA3_PSK: return "WPA3_PSK";
        case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2_WPA3_PSK";
        case WIFI_AUTH_WAPI_PSK: return "WAPI_PSK";
        default: return "UNKNOWN";
    }
}

void restoreWifiMode() {
    // AP scans require AP+STA mode. Keep that mode after the scan instead of
    // toggling the radio back to AP-only: the transition can tear down Bruce's
    // soft AP and strand WebUI clients. Bruce still owns the AP; the idle STA
    // interface is simply left available for later passive surveys.
    if (modeBeforeScan == WIFI_MODE_NULL) {
        WiFi.mode(WIFI_OFF);
    }
}

void failScan(const String &error) {
    WiFi.scanDelete();
    restoreWifiMode();
    heltecFieldLoggerSetWifiScanning(false);
    SurveyLock lock;
    if (!lock) return;
    scanning = false;
    lastError = error;
}

void finishScan(int found) {
    memset(captureNetworks, 0, sizeof(SurveyNetwork) * kResultCapacity);
    const size_t retained = min(static_cast<size_t>(max(found, 0)), kResultCapacity);
    for (size_t index = 0; index < retained; index++) {
        const String ssid = WiFi.SSID(index);
        const String bssid = WiFi.BSSIDstr(index);
        const char *authentication = authenticationName(WiFi.encryptionType(index));
        strlcpy(captureNetworks[index].ssid, ssid.c_str(), sizeof(captureNetworks[index].ssid));
        strlcpy(captureNetworks[index].bssid, bssid.c_str(), sizeof(captureNetworks[index].bssid));
        strlcpy(
            captureNetworks[index].authentication,
            authentication,
            sizeof(captureNetworks[index].authentication)
        );
        captureNetworks[index].rssiDbm = WiFi.RSSI(index);
        captureNetworks[index].channel = static_cast<uint8_t>(WiFi.channel(index));
        captureNetworks[index].hidden = ssid.length() == 0;

        HeltecFieldWifiRecord record;
        record.bssid = bssid;
        record.ssid = ssid;
        record.authentication = authentication;
        record.rssiDbm = captureNetworks[index].rssiDbm;
        record.channel = captureNetworks[index].channel;
        record.hidden = captureNetworks[index].hidden;
        heltecFieldLoggerRecordWifi(record);
        if ((index & 0x0f) == 0) vTaskDelay(1);
    }

    WiFi.scanDelete();
    restoreWifiMode();
    heltecFieldLoggerSetWifiScanning(false);
    const uint32_t now = millis();
    SurveyLock lock;
    if (!lock) return;
    memcpy(networks, captureNetworks, sizeof(SurveyNetwork) * kResultCapacity);
    networkCount = retained;
    droppedNetworks = found > static_cast<int>(kResultCapacity)
                          ? static_cast<uint16_t>(found - kResultCapacity)
                          : 0;
    lastScanDurationMs = now - scanStartedMs;
    lastScanCompletedMs = now;
    scansCompleted++;
    generation++;
    scanning = false;
    lastError = "";
}

bool startScan() {
    {
        SurveyLock lock;
        if (!lock || scanning) return false;
        scanning = true;
        manualScanPending = false;
        scanStartedMs = millis();
        lastError = "";
        modeBeforeScan = WiFi.getMode();
    }

    if (modeBeforeScan == WIFI_MODE_AP) WiFi.mode(WIFI_AP_STA);
    else if (modeBeforeScan == WIFI_MODE_NULL) WiFi.mode(WIFI_STA);
    WiFi.scanDelete();
    heltecFieldLoggerSetWifiScanning(fieldLoggerEnabled);
    const int16_t result = WiFi.scanNetworks(true, true, true, kPassiveDwellMs, 0);
    if (result == WIFI_SCAN_FAILED) {
        failScan("passive WiFi scan could not start");
        return false;
    }
    return true;
}
} // namespace

void heltecMarauderWifiBegin() {
    if (initialized) return;
    if (!surveyMutex) surveyMutex = xSemaphoreCreateMutex();
    if (!networks) {
        networks = static_cast<SurveyNetwork *>(
            psramFound() ? ps_malloc(sizeof(SurveyNetwork) * kResultCapacity)
                         : malloc(sizeof(SurveyNetwork) * kResultCapacity)
        );
    }
    if (!captureNetworks) {
        captureNetworks = static_cast<SurveyNetwork *>(
            psramFound() ? ps_malloc(sizeof(SurveyNetwork) * kResultCapacity)
                         : malloc(sizeof(SurveyNetwork) * kResultCapacity)
        );
    }
    if (!surveyMutex || !networks || !captureNetworks) {
        Serial.println("[UNIFIED] Marauder WiFi survey allocation failed");
        return;
    }
    memset(networks, 0, sizeof(SurveyNetwork) * kResultCapacity);
    memset(captureNetworks, 0, sizeof(SurveyNetwork) * kResultCapacity);
    SurveyLock lock;
    if (!lock) return;
    serviceStartedMs = millis();
    initialized = true;
    Serial.println("[UNIFIED] Passive Marauder WiFi survey initialized");
}

void heltecMarauderWifiPoll() {
    if (!initialized) return;
    const uint32_t now = millis();

    if (now - lastLoggerStateRefreshMs >= kLoggerStateRefreshMs) {
        lastLoggerStateRefreshMs = now;
        fieldLoggerEnabled = heltecFieldLoggerUsesWifi();
    }

    bool scanIsRunning = false;
    {
        SurveyLock lock(pdMS_TO_TICKS(5));
        if (!lock) return;
        scanIsRunning = scanning;
    }
    if (scanIsRunning) {
        const int result = WiFi.scanComplete();
        if (result >= 0) finishScan(result);
        else if (result == WIFI_SCAN_FAILED) failScan("passive WiFi scan failed");
        else if (now - scanStartedMs >= kScanTimeoutMs) failScan("passive WiFi scan timed out");
        return;
    }

    bool shouldStart = false;
    {
        SurveyLock lock(pdMS_TO_TICKS(5));
        if (!lock) return;
        const bool automaticScanReady = now - serviceStartedMs >= kInitialAutoScanDelayMs;
        shouldStart = manualScanPending ||
                      (fieldLoggerEnabled && automaticScanReady &&
                       (lastScanCompletedMs == 0 || now - lastScanCompletedMs >= kLoggerScanIntervalMs));
    }
    if (shouldStart) startScan();
}

bool heltecMarauderWifiRequestScan() {
    SurveyLock lock;
    if (!lock || !initialized || scanning) return false;
    manualScanPending = true;
    return true;
}

bool heltecMarauderWifiClearResults() {
    SurveyLock lock;
    if (!lock || !initialized || scanning) return false;
    memset(networks, 0, sizeof(SurveyNetwork) * kResultCapacity);
    networkCount = 0;
    droppedNetworks = 0;
    generation++;
    return true;
}

HeltecMarauderWifiSnapshot heltecMarauderWifiSnapshot() {
    HeltecMarauderWifiSnapshot snapshot;
    SurveyLock lock;
    if (!lock) return snapshot;
    snapshot.initialized = initialized;
    snapshot.scanning = scanning;
    snapshot.manualScanPending = manualScanPending;
    snapshot.fieldLoggerEnabled = fieldLoggerEnabled;
    snapshot.generation = generation;
    snapshot.scansCompleted = scansCompleted;
    snapshot.lastScanDurationMs = lastScanDurationMs;
    snapshot.lastScanAgeMs = lastScanCompletedMs > 0 ? millis() - lastScanCompletedMs : 0;
    snapshot.networkCount = networkCount;
    snapshot.droppedNetworks = droppedNetworks;
    snapshot.lastError = lastError;
    return snapshot;
}

String heltecMarauderWifiStatusJson() {
    const HeltecMarauderWifiSnapshot snapshot = heltecMarauderWifiSnapshot();
    JsonDocument document;
    document["service"] = "marauder-passive-wifi";
    document["initialized"] = snapshot.initialized;
    document["scanning"] = snapshot.scanning;
    document["pending"] = snapshot.manualScanPending;
    document["passive"] = true;
    document["fieldLoggerEnabled"] = snapshot.fieldLoggerEnabled;
    document["generation"] = snapshot.generation;
    document["scansCompleted"] = snapshot.scansCompleted;
    document["lastScanDurationMs"] = snapshot.lastScanDurationMs;
    document["lastScanAgeMs"] = snapshot.lastScanAgeMs;
    document["networkCount"] = snapshot.networkCount;
    document["capacity"] = kResultCapacity;
    document["droppedNetworks"] = snapshot.droppedNetworks;
    document["scanIntervalMs"] = kLoggerScanIntervalMs;
    document["initialAutoScanDelayMs"] = kInitialAutoScanDelayMs;
    document["initialAutoScanDelayRemainingMs"] =
        millis() - serviceStartedMs >= kInitialAutoScanDelayMs
            ? 0
            : kInitialAutoScanDelayMs - (millis() - serviceStartedMs);
    document["lastError"] = snapshot.lastError;
    String output;
    serializeJson(document, output);
    return output;
}

String heltecMarauderWifiResultsJson() {
    JsonDocument document;
    SurveyLock lock;
    if (!lock) {
        document["error"] = "WiFi survey busy";
    } else {
        document["generation"] = generation;
        document["count"] = networkCount;
        document["capacity"] = kResultCapacity;
        document["droppedNetworks"] = droppedNetworks;
        JsonArray results = document["networks"].to<JsonArray>();
        for (size_t index = 0; index < networkCount; index++) {
            JsonObject network = results.add<JsonObject>();
            network["bssid"] = networks[index].bssid;
            network["ssid"] = networks[index].ssid;
            network["authentication"] = networks[index].authentication;
            network["rssiDbm"] = networks[index].rssiDbm;
            network["channel"] = networks[index].channel;
            network["hidden"] = networks[index].hidden;
        }
    }
    String output;
    serializeJson(document, output);
    return output;
}

String heltecUnifiedCapabilitiesJson() {
    JsonDocument document;
    document["apiVersion"] = 2;
    document["firmwareFamily"] = "Bruce + Marauder Unified";
    document["board"] = "Heltec WiFi LoRa 32 V4";
    document["sources"]["shell"] = "Bruce";
    document["sources"]["wifiSurvey"] = "ESP32 Marauder";
    document["sharedOwners"]["webApi"] = "Bruce";
    document["sharedOwners"]["displayAndButton"] = "Bruce Heltec";
    document["sharedOwners"]["gpsAndFieldLog"] = "Bruce Heltec";
    document["sharedOwners"]["wifiDeviceList"] = "Marauder passive survey";
    document["sharedOwners"]["bleScanner"] = "Bruce field logger";
    document["sharedOwners"]["lora"] = "Bruce SX1262";
    JsonArray capabilities = document["capabilities"].to<JsonArray>();
    capabilities.add("authenticated-web-api");
    capabilities.add("passive-wifi-survey");
    capabilities.add("wifi-device-list");
    capabilities.add("reset-resistant-wifi-gps-ble-field-log");
    capabilities.add("authenticated-android-gps-assist");
    capabilities.add("gps-track");
    capabilities.add("ble-observations");
    capabilities.add("sx1262-lora");
    capabilities.add("usb-serial-cli");
    document["constraints"]["wifiRadio"] =
        "channel-hopping monitor mode cannot preserve the BruceNet AP";
    document["constraints"]["surveyMode"] = "passive receive only";
    document["constraints"]["phoneGps"] =
        "requires an active field log with GPS enabled";
    String output;
    serializeJson(document, output);
    return output;
}
