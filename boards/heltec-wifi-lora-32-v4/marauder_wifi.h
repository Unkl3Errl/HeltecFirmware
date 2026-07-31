#pragma once

#include <Arduino.h>

struct HeltecMarauderWifiSnapshot {
    bool initialized = false;
    bool scanning = false;
    bool manualScanPending = false;
    bool fieldLoggerEnabled = false;
    uint32_t generation = 0;
    uint32_t scansCompleted = 0;
    uint32_t lastScanDurationMs = 0;
    uint32_t lastScanAgeMs = 0;
    uint16_t networkCount = 0;
    uint16_t droppedNetworks = 0;
    String lastError;
};

void heltecMarauderWifiBegin();
void heltecMarauderWifiPoll();
bool heltecMarauderWifiRequestScan();
bool heltecMarauderWifiClearResults();
HeltecMarauderWifiSnapshot heltecMarauderWifiSnapshot();
String heltecMarauderWifiStatusJson();
String heltecMarauderWifiResultsJson();
String heltecUnifiedCapabilitiesJson();
