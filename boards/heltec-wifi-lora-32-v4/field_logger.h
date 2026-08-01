#pragma once

#include <Arduino.h>

struct HeltecFieldGpsRecord {
    String source = "onboard";
    String provider;
    uint32_t uptimeMs = 0;
    uint32_t sequence = 0;
    uint64_t sourceUnixTimeMs = 0;
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
    double accuracyMeters = NAN;
};

struct HeltecFieldWifiRecord {
    String source = "android";
    String bssid;
    String ssid;
    String capabilities;
    uint32_t sequence = 0;
    uint32_t scanSequence = 0;
    uint64_t sourceUnixTimeMs = 0;
    int32_t rssiDbm = 0;
    uint32_t frequencyMhz = 0;
    int32_t channelWidth = -1;
    uint32_t centerFrequency0Mhz = 0;
    uint32_t centerFrequency1Mhz = 0;
};

struct HeltecFieldLogSnapshot {
    bool initialized = false;
    bool active = false;
    bool autoResume = false;
    bool gpsEnabled = false;
    bool gpsOwned = false;
    bool bleEnabled = false;
    bool bleScanning = false;
    bool uniqueBleCapacityReached = false;
    bool wifiEnabled = false;
    bool uniqueWifiCapacityReached = false;
    uint32_t sessionId = 0;
    uint32_t segment = 0;
    uint32_t bootCount = 0;
    uint32_t resumeCount = 0;
    uint32_t recoveredSegments = 0;
    uint32_t gpsFixes = 0;
    uint32_t phoneGpsFixes = 0;
    uint32_t bleObservations = 0;
    uint32_t uniqueBleDevices = 0;
    uint32_t wifiObservations = 0;
    uint32_t uniqueWifiNetworks = 0;
    uint32_t startedAtMs = 0;
    uint32_t lastGpsAtMs = 0;
    uint32_t lastBleAtMs = 0;
    uint32_t lastWifiAtMs = 0;
    size_t sessionBytes = 0;
    String fileName;
    String lastError;
    String resetReason;
};

void heltecFieldLoggerBegin();
void heltecFieldLoggerPoll();
bool heltecFieldLoggerStart(
    bool gpsEnabled,
    bool bleEnabled,
    bool wifiEnabled,
    bool autoResume
);
bool heltecFieldLoggerStop();
void heltecFieldLoggerSuspendForSleep();
bool heltecFieldLoggerRecordGps(const HeltecFieldGpsRecord &record);
bool heltecFieldLoggerRecordWifi(const HeltecFieldWifiRecord &record);
HeltecFieldLogSnapshot heltecFieldLoggerSnapshot();
String heltecFieldLoggerStatusJson();
String heltecFieldLoggerFilesJson();
String heltecFieldLoggerDownloadPath(const String &fileName);
bool heltecFieldLoggerIsActive();
bool heltecFieldLoggerUsesGps();
