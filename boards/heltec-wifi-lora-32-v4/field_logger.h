#pragma once

#include <Arduino.h>

struct HeltecFieldGpsRecord {
    uint32_t uptimeMs = 0;
    uint32_t sequence = 0;
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

struct HeltecFieldLogSnapshot {
    bool initialized = false;
    bool active = false;
    bool autoResume = false;
    bool gpsEnabled = false;
    bool gpsOwned = false;
    bool bleEnabled = false;
    bool bleScanning = false;
    bool uniqueBleCapacityReached = false;
    uint32_t sessionId = 0;
    uint32_t segment = 0;
    uint32_t bootCount = 0;
    uint32_t resumeCount = 0;
    uint32_t recoveredSegments = 0;
    uint32_t gpsFixes = 0;
    uint32_t bleObservations = 0;
    uint32_t uniqueBleDevices = 0;
    uint32_t startedAtMs = 0;
    uint32_t lastGpsAtMs = 0;
    uint32_t lastBleAtMs = 0;
    size_t sessionBytes = 0;
    String fileName;
    String lastError;
    String resetReason;
};

void heltecFieldLoggerBegin();
void heltecFieldLoggerPoll();
bool heltecFieldLoggerStart(bool gpsEnabled, bool bleEnabled, bool autoResume);
bool heltecFieldLoggerStop();
void heltecFieldLoggerSuspendForSleep();
void heltecFieldLoggerRecordGps(const HeltecFieldGpsRecord &record);
HeltecFieldLogSnapshot heltecFieldLoggerSnapshot();
String heltecFieldLoggerStatusJson();
String heltecFieldLoggerFilesJson();
String heltecFieldLoggerDownloadPath(const String &fileName);
bool heltecFieldLoggerIsActive();
bool heltecFieldLoggerUsesGps();
