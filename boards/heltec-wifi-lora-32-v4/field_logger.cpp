#include "field_logger.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <esp_system.h>

extern bool heltecV4GpsMonitorActive();
extern bool heltecV4SetGpsMonitor(bool enabled);
extern bool isBLEAPIEnabled();

namespace {
constexpr char kDirectory[] = "/BruceFieldLogs";
constexpr char kPreferencesNamespace[] = "hl_field";
constexpr uint32_t kFormatVersion = 1;
constexpr size_t kMinimumFreeBytes = 256 * 1024;
constexpr size_t kMaximumListedFiles = 64;
constexpr size_t kUniqueBleCapacity = 256;
constexpr size_t kRecentBleCapacity = 128;
constexpr size_t kReconstructionReadBlockBytes = 4096;
constexpr uint32_t kBleObservationIntervalMs = 60 * 1000;
constexpr uint32_t kBleScanDurationMs = 5000;
constexpr uint32_t kBleScanPauseMs = 10000;
constexpr uint32_t kGpsAssociationMaximumAgeMs = 30000;

struct RecentBleDevice {
    uint32_t hash = 0;
    uint32_t lastSeenMs = 0;
};

struct StoredLogHighWater {
    uint32_t session = 0;
    uint32_t segment = 0;
    String path;
};

SemaphoreHandle_t loggerMutex = nullptr;
Preferences preferences;
TaskHandle_t bleTaskHandle = nullptr;
volatile bool bleStopRequested = false;
bool bleInitializedByLogger = false;
bool cleanupRequested = false;

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
String currentPath;
String lastError;
String resetReason;
uint32_t uniqueBleHashes[kUniqueBleCapacity] = {};
RecentBleDevice recentBleDevices[kRecentBleCapacity] = {};
size_t recentBleNext = 0;
bool latestGpsValid = false;
double latestLatitude = 0.0;
double latestLongitude = 0.0;
uint32_t latestGpsAtMs = 0;

class LoggerLock {
public:
    explicit LoggerLock(TickType_t wait = pdMS_TO_TICKS(1000)) {
        locked = loggerMutex && xSemaphoreTake(loggerMutex, wait) == pdTRUE;
    }
    ~LoggerLock() {
        if (locked) xSemaphoreGive(loggerMutex);
    }
    explicit operator bool() const { return locked; }

private:
    bool locked = false;
};

String basenameOf(const String &path) {
    const int slash = path.lastIndexOf('/');
    return slash >= 0 ? path.substring(slash + 1) : path;
}

String makePath(uint32_t id, uint32_t part) {
    char path[64];
    snprintf(
        path,
        sizeof(path),
        "%s/session-%06lu-%03lu.ndjson",
        kDirectory,
        static_cast<unsigned long>(id),
        static_cast<unsigned long>(part)
    );
    return String(path);
}

String sessionPrefix(uint32_t id) {
    char prefix[32];
    snprintf(prefix, sizeof(prefix), "session-%06lu-", static_cast<unsigned long>(id));
    return String(prefix);
}

bool isSafeLogPath(const String &path) {
    return path.startsWith(String(kDirectory) + "/session-") && path.endsWith(".ndjson") &&
           path.indexOf("..") < 0;
}

bool isSafeFileName(const String &name) {
    if (!name.startsWith("session-") || !name.endsWith(".ndjson") || name.length() > 48) return false;
    for (size_t index = 0; index < name.length(); index++) {
        const char c = name[index];
        if (!isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_' && c != '.') return false;
    }
    return true;
}

bool parseLogFileName(const String &name, uint32_t &parsedSession, uint32_t &parsedSegment) {
    if (!isSafeFileName(name)) return false;
    unsigned long sessionValue = 0;
    unsigned long segmentValue = 0;
    char trailing = '\0';
    if (sscanf(name.c_str(), "session-%lu-%lu.ndjson%c", &sessionValue, &segmentValue, &trailing) != 2) {
        return false;
    }
    parsedSession = static_cast<uint32_t>(sessionValue);
    parsedSegment = static_cast<uint32_t>(segmentValue);
    return true;
}

StoredLogHighWater storedLogHighWaterLocked() {
    StoredLogHighWater highWater;
    File directory = LittleFS.open(kDirectory);
    if (!directory || !directory.isDirectory()) return highWater;
    File entry = directory.openNextFile();
    while (entry) {
        const String name = basenameOf(entry.path());
        uint32_t storedSession = 0;
        uint32_t storedSegment = 0;
        if (
            !entry.isDirectory() && parseLogFileName(name, storedSession, storedSegment) &&
            (storedSession > highWater.session ||
             (storedSession == highWater.session && storedSegment > highWater.segment))
        ) {
            highWater.session = storedSession;
            highWater.segment = storedSegment;
            highWater.path = String(kDirectory) + "/" + name;
        }
        entry.close();
        entry = directory.openNextFile();
    }
    directory.close();
    return highWater;
}

const char *resetReasonName(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON: return "power_on";
        case ESP_RST_EXT: return "external_reset";
        case ESP_RST_SW: return "software_reset";
        case ESP_RST_PANIC: return "panic";
        case ESP_RST_INT_WDT: return "interrupt_watchdog";
        case ESP_RST_TASK_WDT: return "task_watchdog";
        case ESP_RST_WDT: return "watchdog";
        case ESP_RST_DEEPSLEEP: return "deep_sleep";
        case ESP_RST_BROWNOUT: return "brownout";
        case ESP_RST_SDIO: return "sdio";
        default: return "unknown";
    }
}

uint32_t hashBleAddress(const String &address) {
    uint32_t hash = 2166136261u;
    for (size_t index = 0; index < address.length(); index++) {
        hash ^= static_cast<uint8_t>(address[index]);
        hash *= 16777619u;
    }
    return hash == 0 ? 1 : hash;
}

void clearSessionStateLocked() {
    resumeCount = 0;
    recoveredSegments = 0;
    gpsFixes = 0;
    bleObservations = 0;
    uniqueBleDevices = 0;
    uniqueBleCapacityReached = false;
    startedAtMs = millis();
    lastGpsAtMs = 0;
    lastBleAtMs = 0;
    sessionBytes = 0;
    lastError = "";
    latestGpsValid = false;
    latestGpsAtMs = 0;
    memset(uniqueBleHashes, 0, sizeof(uniqueBleHashes));
    memset(recentBleDevices, 0, sizeof(recentBleDevices));
    recentBleNext = 0;
}

void noteUniqueBleLocked(uint32_t hash) {
    for (size_t index = 0; index < uniqueBleDevices; index++) {
        if (uniqueBleHashes[index] == hash) return;
    }
    if (uniqueBleDevices < kUniqueBleCapacity) {
        uniqueBleHashes[uniqueBleDevices++] = hash;
    } else {
        uniqueBleCapacityReached = true;
    }
}

bool shouldRecordBleLocked(uint32_t hash, uint32_t now) {
    for (RecentBleDevice &device : recentBleDevices) {
        if (device.hash != hash) continue;
        if (now - device.lastSeenMs < kBleObservationIntervalMs) return false;
        device.lastSeenMs = now;
        return true;
    }
    recentBleDevices[recentBleNext] = {hash, now};
    recentBleNext = (recentBleNext + 1) % kRecentBleCapacity;
    return true;
}

void addCommonFieldsLocked(JsonDocument &document, const char *type) {
    document["formatVersion"] = kFormatVersion;
    document["type"] = type;
    document["sessionId"] = sessionId;
    document["segment"] = segment;
    document["bootCount"] = bootCount;
    document["uptimeMs"] = millis();
}

bool appendRecordLocked(JsonDocument &document) {
    if (!isSafeLogPath(currentPath)) {
        lastError = "invalid active log path";
        active = false;
        cleanupRequested = true;
        bleStopRequested = true;
        preferences.putBool("active", false);
        return false;
    }
    const size_t recordBytes = measureJson(document) + 1;
    const size_t total = LittleFS.totalBytes();
    const size_t used = LittleFS.usedBytes();
    if (total <= used || total - used < kMinimumFreeBytes + recordBytes) {
        lastError = "LittleFS reserve reached; field logging stopped";
        active = false;
        cleanupRequested = true;
        bleStopRequested = true;
        preferences.putBool("active", false);
        return false;
    }

    File file = LittleFS.open(currentPath, FILE_APPEND, true);
    if (!file) {
        lastError = "could not open field log for append";
        active = false;
        cleanupRequested = true;
        bleStopRequested = true;
        preferences.putBool("active", false);
        return false;
    }
    const size_t written = serializeJson(document, file);
    const size_t newlineWritten = file.print('\n');
    file.flush();
    file.close();
    if (written + newlineWritten != recordBytes) {
        lastError = "incomplete field log write";
        active = false;
        cleanupRequested = true;
        bleStopRequested = true;
        preferences.putBool("active", false);
        return false;
    }
    sessionBytes += recordBytes;
    return true;
}

bool fileHasCompleteTail(const String &path) {
    File file = LittleFS.open(path, FILE_READ);
    if (!file) return true;
    const size_t size = file.size();
    if (size == 0) {
        file.close();
        return true;
    }
    if (!file.seek(size - 1)) {
        file.close();
        return false;
    }
    const bool complete = file.read() == '\n';
    file.close();
    return complete;
}

void countRecordLocked(JsonDocument &document) {
    const char *type = document["type"] | "";
    if (!strcmp(type, "gps")) {
        gpsFixes++;
    } else if (!strcmp(type, "ble")) {
        bleObservations++;
        const char *address = document["address"] | "";
        if (*address) noteUniqueBleLocked(hashBleAddress(String(address)));
    } else if (!strcmp(type, "session_resume")) {
        resumeCount++;
    } else if (!strcmp(type, "tail_recovery")) {
        recoveredSegments++;
        resumeCount++;
    }
}

bool extractJsonStringField(const String &line, const char *marker, String &value) {
    const int markerAt = line.indexOf(marker);
    if (markerAt < 0) return false;
    const int valueAt = markerAt + strlen(marker);
    const int valueEnd = line.indexOf('"', valueAt);
    if (valueEnd < valueAt) return false;
    value = line.substring(valueAt, valueEnd);
    return true;
}

bool countGeneratedRecordLineLocked(const String &line) {
    if (!line.startsWith("{") || !line.endsWith("}") ||
        line.indexOf("\"formatVersion\":1") < 0) {
        return false;
    }

    if (line.indexOf("\"type\":\"gps\"") >= 0) {
        gpsFixes++;
        if (line.indexOf("\"source\":\"android\"") >= 0) phoneGpsFixes++;
        return true;
    }
    if (line.indexOf("\"type\":\"ble\"") >= 0) {
        bleObservations++;
        String address;
        if (extractJsonStringField(line, "\"address\":\"", address) && address.length() == 17) {
            noteUniqueBleLocked(hashBleAddress(address));
        }
        return true;
    }
    if (line.indexOf("\"type\":\"session_resume\"") >= 0) {
        resumeCount++;
        return true;
    }
    if (line.indexOf("\"type\":\"tail_recovery\"") >= 0) {
        recoveredSegments++;
        resumeCount++;
        return true;
    }
    return line.indexOf("\"type\":\"session_start\"") >= 0 ||
           line.indexOf("\"type\":\"session_stop\"") >= 0 ||
           line.indexOf("\"type\":\"session_suspend\"") >= 0 ||
           line.indexOf("\"type\":\"session_interrupted\"") >= 0;
}

void countReconstructedLineLocked(const String &line, size_t &reconstructedRecords) {
    if (line.length() == 0) return;
    if (!countGeneratedRecordLineLocked(line)) {
        JsonDocument document;
        if (deserializeJson(document, line) == DeserializationError::Ok) {
            countRecordLocked(document);
        }
    }
    reconstructedRecords++;
    if ((reconstructedRecords & 0x3ff) == 0) delay(1);
}

void reconstructSessionLocked() {
    clearSessionStateLocked();
    const uint32_t reconstructionStartedMs = millis();
    size_t reconstructedRecords = 0;
    static uint8_t readBlock[kReconstructionReadBlockBytes];
    const String prefix = sessionPrefix(sessionId);
    File directory = LittleFS.open(kDirectory);
    if (!directory || !directory.isDirectory()) return;

    File entry = directory.openNextFile();
    while (entry) {
        const String name = basenameOf(entry.path());
        if (!entry.isDirectory() && name.startsWith(prefix) && name.endsWith(".ndjson")) {
            entry.setBufferSize(kReconstructionReadBlockBytes);
            sessionBytes += entry.size();
            String line;
            line.reserve(512);
            size_t bytesRead = 0;
            while ((bytesRead = entry.read(readBlock, sizeof(readBlock))) > 0) {
                size_t lineStart = 0;
                for (size_t index = 0; index < bytesRead; index++) {
                    if (readBlock[index] != '\n') continue;
                    if (index > lineStart) {
                        line.concat(
                            reinterpret_cast<const char *>(&readBlock[lineStart]),
                            static_cast<unsigned int>(index - lineStart)
                        );
                    }
                    countReconstructedLineLocked(line, reconstructedRecords);
                    line = "";
                    lineStart = index + 1;
                }
                if (lineStart < bytesRead) {
                    line.concat(
                        reinterpret_cast<const char *>(&readBlock[lineStart]),
                        static_cast<unsigned int>(bytesRead - lineStart)
                    );
                }
            }
            // A non-empty remainder has no newline and is therefore an interrupted
            // record. It is deliberately excluded, matching the tail-recovery rules.
        }
        entry.close();
        entry = directory.openNextFile();
    }
    directory.close();
    Serial.printf(
        "[HELTEC] Reconstructed %u field-log records / %u bytes in %lu ms\n",
        static_cast<unsigned>(reconstructedRecords),
        static_cast<unsigned>(sessionBytes),
        static_cast<unsigned long>(millis() - reconstructionStartedMs)
    );
}

void persistSessionLocked() {
    preferences.putBool("active", active);
    preferences.putBool("auto", autoResume);
    preferences.putBool("gps", gpsEnabled);
    preferences.putBool("ble", bleEnabled);
    preferences.putUInt("session", sessionId);
    preferences.putUInt("segment", segment);
    preferences.putString("path", currentPath);
}

bool loggerActive() {
    LoggerLock lock(pdMS_TO_TICKS(100));
    return lock && active;
}

void setBleScanning(bool scanning) {
    LoggerLock lock;
    if (lock) bleScanning = scanning;
}

void setBleError(const String &error) {
    LoggerLock lock;
    if (lock) lastError = error;
}

void recordBleDevice(const NimBLEAdvertisedDevice &device) {
    const String address(device.getAddress().toString().c_str());
    const uint32_t addressHash = hashBleAddress(address);
    const uint32_t now = millis();

    LoggerLock lock;
    if (!lock || !active || !bleEnabled || !shouldRecordBleLocked(addressHash, now)) return;

    JsonDocument document;
    addCommonFieldsLocked(document, "ble");
    document["address"] = address;
    document["addressType"] = device.getAddressType();
    document["rssiDbm"] = device.getRSSI();
    document["connectable"] = device.isConnectable();
    if (device.haveName()) document["name"] = String(device.getName().c_str()).substring(0, 64);
    if (device.haveTXPower()) document["txPowerDbm"] = device.getTXPower();
    if (device.haveManufacturerData()) {
        const std::string manufacturer = device.getManufacturerData();
        if (manufacturer.size() >= 2) {
            document["manufacturerId"] =
                static_cast<uint16_t>(static_cast<uint8_t>(manufacturer[0])) |
                static_cast<uint16_t>(static_cast<uint8_t>(manufacturer[1]) << 8);
        }
    }
    if (latestGpsValid && now - latestGpsAtMs <= kGpsAssociationMaximumAgeMs) {
        document["location"]["latitude"] = latestLatitude;
        document["location"]["longitude"] = latestLongitude;
        document["location"]["ageMs"] = now - latestGpsAtMs;
    }
    if (appendRecordLocked(document)) {
        bleObservations++;
        noteUniqueBleLocked(addressHash);
        lastBleAtMs = now;
    }
}

void bleLoggerTask(void *) {
    while (!bleStopRequested && loggerActive()) {
        if (!NimBLEDevice::isInitialized()) {
            if (!NimBLEDevice::init("")) {
                setBleError("BLE initialization failed");
                break;
            }
            LoggerLock lock;
            if (lock) bleInitializedByLogger = true;
        }

        NimBLEScan *scan = NimBLEDevice::getScan();
        if (!scan) {
            setBleError("BLE scanner unavailable");
            break;
        }
        scan->setActiveScan(false);
        scan->setInterval(320);
        scan->setWindow(80);
        scan->setMaxResults(96);
        setBleScanning(true);
        const NimBLEScanResults results = scan->getResults(kBleScanDurationMs, false);
        setBleScanning(false);

        if (bleStopRequested || !loggerActive()) {
            scan->clearResults();
            break;
        }
        for (int index = 0; index < results.getCount(); index++) {
            const NimBLEAdvertisedDevice *device = results.getDevice(index);
            if (device && !bleStopRequested && loggerActive()) recordBleDevice(*device);
        }
        scan->clearResults();

        for (uint32_t waited = 0; waited < kBleScanPauseMs && !bleStopRequested && loggerActive(); waited += 250) {
            vTaskDelay(pdMS_TO_TICKS(250));
        }
    }

    setBleScanning(false);
    bool releaseBle = false;
    {
        LoggerLock lock;
        if (lock) {
            bleTaskHandle = nullptr;
            releaseBle = bleInitializedByLogger && !active && !isBLEAPIEnabled();
            if (releaseBle) bleInitializedByLogger = false;
        }
    }
    if (releaseBle && NimBLEDevice::isInitialized()) NimBLEDevice::deinit(true);
    vTaskDelete(nullptr);
}

void startSelectedServices(bool startGps, bool startBle) {
    if (startGps) {
        const bool alreadyActive = heltecV4GpsMonitorActive();
        const bool ok = heltecV4SetGpsMonitor(true);
        LoggerLock lock;
        if (lock) {
            gpsOwned = ok && !alreadyActive;
            if (!ok) lastError = "GPS monitor could not start";
        }
    }
    if (startBle) {
        LoggerLock lock;
        if (!lock) return;
        if (!bleTaskHandle) {
            bleStopRequested = false;
            if (xTaskCreate(bleLoggerTask, "HeltecFieldBLE", 8192, nullptr, 1, &bleTaskHandle) != pdPASS) {
                bleTaskHandle = nullptr;
                lastError = "BLE logger task could not start";
            }
        }
    }
}

void stopSelectedServices(bool stopGps) {
    bleStopRequested = true;
    if (NimBLEDevice::isInitialized()) {
        NimBLEScan *scan = NimBLEDevice::getScan();
        if (scan && scan->isScanning()) scan->stop();
    }
    if (stopGps) heltecV4SetGpsMonitor(false);
}
} // namespace

void heltecFieldLoggerBegin() {
    if (initialized) return;
    if (!loggerMutex) loggerMutex = xSemaphoreCreateMutex();
    if (!loggerMutex || !preferences.begin(kPreferencesNamespace, false)) {
        Serial.println("[HELTEC] Field logger initialization failed");
        return;
    }

    bool resumeServices = false;
    bool resumeGps = false;
    bool resumeBle = false;
    {
        LoggerLock lock;
        if (!lock) return;
        initialized = true;
        resetReason = resetReasonName(esp_reset_reason());
        bootCount = preferences.getUInt("boots", 0) + 1;
        preferences.putUInt("boots", bootCount);
        active = preferences.getBool("active", false);
        autoResume = preferences.getBool("auto", true);
        gpsEnabled = preferences.getBool("gps", true);
        bleEnabled = preferences.getBool("ble", true);
        sessionId = preferences.getUInt("session", 0);
        segment = preferences.getUInt("segment", 0);
        currentPath = preferences.getString("path", "");
        if (!isSafeLogPath(currentPath)) currentPath = makePath(sessionId, segment);

        if (active) {
            reconstructSessionLocked();
            if (!autoResume) {
                JsonDocument document;
                addCommonFieldsLocked(document, "session_interrupted");
                document["resetReason"] = resetReason;
                appendRecordLocked(document);
                active = false;
                persistSessionLocked();
            } else {
                const bool interruptedTail = LittleFS.exists(currentPath) && !fileHasCompleteTail(currentPath);
                if (interruptedTail) {
                    segment++;
                    recoveredSegments++;
                    currentPath = makePath(sessionId, segment);
                    persistSessionLocked();
                }
                JsonDocument document;
                addCommonFieldsLocked(document, interruptedTail ? "tail_recovery" : "session_resume");
                document["resetReason"] = resetReason;
                if (interruptedTail) document["previousSegment"] = segment - 1;
                if (appendRecordLocked(document)) {
                    resumeCount++;
                    resumeServices = true;
                    resumeGps = gpsEnabled;
                    resumeBle = bleEnabled;
                }
            }
        } else {
            const StoredLogHighWater highWater = storedLogHighWaterLocked();
            if (highWater.session >= sessionId && highWater.session > 0) {
                sessionId = highWater.session;
                segment = highWater.segment;
                currentPath = highWater.path;
                reconstructSessionLocked();
                persistSessionLocked();
            } else {
                clearSessionStateLocked();
            }
        }
    }

    if (resumeServices) startSelectedServices(resumeGps, resumeBle);
    Serial.printf(
        "[HELTEC] Field logger: %s, session=%lu, reset=%s\n",
        resumeServices ? "resumed" : "idle",
        static_cast<unsigned long>(sessionId),
        resetReason.c_str()
    );
}

void heltecFieldLoggerPoll() {
    bool stopGps = false;
    {
        LoggerLock lock(pdMS_TO_TICKS(10));
        if (!lock || !cleanupRequested) return;
        cleanupRequested = false;
        stopGps = gpsOwned;
        gpsOwned = false;
    }
    stopSelectedServices(stopGps);
}

bool heltecFieldLoggerStart(bool enableGps, bool enableBle, bool enableAutoResume) {
    bool startServices = false;
    {
        LoggerLock lock;
        if (!lock || !initialized) return false;
        if (active) return true;
        if (!enableGps && !enableBle) {
            lastError = "select GPS, BLE, or both";
            return false;
        }
        if (!LittleFS.exists(kDirectory) && !LittleFS.mkdir(kDirectory)) {
            lastError = "could not create field log directory";
            return false;
        }
        const size_t total = LittleFS.totalBytes();
        const size_t used = LittleFS.usedBytes();
        if (total <= used || total - used < kMinimumFreeBytes) {
            lastError = "not enough LittleFS space";
            return false;
        }

        const StoredLogHighWater highWater = storedLogHighWaterLocked();
        const uint32_t persistedSession = preferences.getUInt("session", 0);
        const uint32_t lastSession = highWater.session > persistedSession ? highWater.session : persistedSession;
        if (lastSession == UINT32_MAX) {
            lastError = "field log session counter exhausted";
            return false;
        }
        sessionId = lastSession + 1;
        segment = 0;
        currentPath = makePath(sessionId, segment);
        clearSessionStateLocked();
        cleanupRequested = false;
        active = true;
        autoResume = enableAutoResume;
        gpsEnabled = enableGps;
        bleEnabled = enableBle;
        gpsOwned = false;
        persistSessionLocked();

        JsonDocument document;
        addCommonFieldsLocked(document, "session_start");
        document["gpsEnabled"] = gpsEnabled;
        document["bleEnabled"] = bleEnabled;
        document["autoResume"] = autoResume;
        document["resetReason"] = resetReason;
        if (!appendRecordLocked(document)) {
            active = false;
            persistSessionLocked();
            return false;
        }
        startServices = true;
    }

    if (startServices) startSelectedServices(enableGps, enableBle);
    return true;
}

bool heltecFieldLoggerStop() {
    bool stopGps = false;
    {
        LoggerLock lock;
        if (!lock || !initialized) return false;
        if (!active) return true;
        active = false;
        persistSessionLocked();
        JsonDocument document;
        addCommonFieldsLocked(document, "session_stop");
        document["reason"] = "user";
        appendRecordLocked(document);
        stopGps = gpsOwned;
        gpsOwned = false;
    }
    stopSelectedServices(stopGps);
    return true;
}

void heltecFieldLoggerSuspendForSleep() {
    bool stopGps = false;
    {
        LoggerLock lock;
        if (!lock || !initialized || !active) return;
        JsonDocument document;
        addCommonFieldsLocked(document, autoResume ? "session_suspend" : "session_stop");
        document["reason"] = "deep_sleep";
        appendRecordLocked(document);
        stopGps = gpsOwned;
        gpsOwned = false;
        if (!autoResume) active = false;
        persistSessionLocked();
    }
    stopSelectedServices(stopGps);
}

void heltecFieldLoggerRecordGps(const HeltecFieldGpsRecord &record) {
    LoggerLock lock;
    if (!lock || !active || !gpsEnabled) return;

    JsonDocument document;
    addCommonFieldsLocked(document, "gps");
    document["sourceUptimeMs"] = record.uptimeMs;
    document["sequence"] = record.sequence;
    document["latitude"] = record.latitude;
    document["longitude"] = record.longitude;
    document["satellites"] = record.satellites;
    if (record.utcValid) {
        char utc[24];
        snprintf(
            utc,
            sizeof(utc),
            "%04u-%02u-%02uT%02u:%02u:%02u.%02uZ",
            static_cast<unsigned>(record.utcYear),
            static_cast<unsigned>(record.utcMonth),
            static_cast<unsigned>(record.utcDay),
            static_cast<unsigned>(record.utcHour),
            static_cast<unsigned>(record.utcMinute),
            static_cast<unsigned>(record.utcSecond),
            static_cast<unsigned>(record.utcCentisecond)
        );
        document["utc"] = utc;
    }
    if (!isnan(record.altitudeMeters)) document["altitudeMeters"] = record.altitudeMeters;
    if (!isnan(record.speedKmph)) document["speedKmph"] = record.speedKmph;
    if (!isnan(record.hdop)) document["hdop"] = record.hdop;
    if (appendRecordLocked(document)) {
        gpsFixes++;
        lastGpsAtMs = record.uptimeMs;
        latestGpsValid = true;
        latestLatitude = record.latitude;
        latestLongitude = record.longitude;
        latestGpsAtMs = record.uptimeMs;
    }
}

HeltecFieldLogSnapshot heltecFieldLoggerSnapshot() {
    HeltecFieldLogSnapshot snapshot;
    LoggerLock lock;
    if (!lock) return snapshot;
    snapshot.initialized = initialized;
    snapshot.active = active;
    snapshot.autoResume = autoResume;
    snapshot.gpsEnabled = gpsEnabled;
    snapshot.gpsOwned = gpsOwned;
    snapshot.bleEnabled = bleEnabled;
    snapshot.bleScanning = bleScanning;
    snapshot.uniqueBleCapacityReached = uniqueBleCapacityReached;
    snapshot.sessionId = sessionId;
    snapshot.segment = segment;
    snapshot.bootCount = bootCount;
    snapshot.resumeCount = resumeCount;
    snapshot.recoveredSegments = recoveredSegments;
    snapshot.gpsFixes = gpsFixes;
    snapshot.bleObservations = bleObservations;
    snapshot.uniqueBleDevices = uniqueBleDevices;
    snapshot.startedAtMs = startedAtMs;
    snapshot.lastGpsAtMs = lastGpsAtMs;
    snapshot.lastBleAtMs = lastBleAtMs;
    snapshot.sessionBytes = sessionBytes;
    snapshot.fileName = basenameOf(currentPath);
    snapshot.lastError = lastError;
    snapshot.resetReason = resetReason;
    return snapshot;
}

String heltecFieldLoggerStatusJson() {
    const HeltecFieldLogSnapshot snapshot = heltecFieldLoggerSnapshot();
    JsonDocument document;
    document["formatVersion"] = kFormatVersion;
    document["initialized"] = snapshot.initialized;
    document["active"] = snapshot.active;
    document["autoResume"] = snapshot.autoResume;
    document["sessionId"] = snapshot.sessionId;
    document["segment"] = snapshot.segment;
    document["bootCount"] = snapshot.bootCount;
    document["resumeCount"] = snapshot.resumeCount;
    document["recoveredSegments"] = snapshot.recoveredSegments;
    document["startedAtMs"] = snapshot.startedAtMs;
    document["gps"]["enabled"] = snapshot.gpsEnabled;
    document["gps"]["owned"] = snapshot.gpsOwned;
    document["gps"]["fixes"] = snapshot.gpsFixes;
    document["gps"]["lastRecordAgeMs"] =
        snapshot.lastGpsAtMs > 0 ? millis() - snapshot.lastGpsAtMs : 0;
    document["ble"]["enabled"] = snapshot.bleEnabled;
    document["ble"]["scanning"] = snapshot.bleScanning;
    document["ble"]["observations"] = snapshot.bleObservations;
    document["ble"]["uniqueDevices"] = snapshot.uniqueBleDevices;
    document["ble"]["uniqueCapacity"] = kUniqueBleCapacity;
    document["ble"]["uniqueCapacityReached"] = snapshot.uniqueBleCapacityReached;
    document["ble"]["lastRecordAgeMs"] =
        snapshot.lastBleAtMs > 0 ? millis() - snapshot.lastBleAtMs : 0;
    document["storage"]["directory"] = kDirectory;
    document["storage"]["fileName"] = snapshot.fileName;
    document["storage"]["sessionBytes"] = static_cast<uint64_t>(snapshot.sessionBytes);
    document["storage"]["totalBytes"] = static_cast<uint64_t>(LittleFS.totalBytes());
    document["storage"]["usedBytes"] = static_cast<uint64_t>(LittleFS.usedBytes());
    document["storage"]["minimumFreeBytes"] = kMinimumFreeBytes;
    document["resetReason"] = snapshot.resetReason;
    document["lastError"] = snapshot.lastError;
    String output;
    serializeJson(document, output);
    return output;
}

String heltecFieldLoggerFilesJson() {
    LoggerLock lock;
    JsonDocument document;
    document["directory"] = kDirectory;
    document["totalBytes"] = static_cast<uint64_t>(LittleFS.totalBytes());
    document["usedBytes"] = static_cast<uint64_t>(LittleFS.usedBytes());
    JsonArray files = document["files"].to<JsonArray>();
    size_t listed = 0;
    size_t totalFiles = 0;
    File directory = LittleFS.open(kDirectory);
    if (directory && directory.isDirectory()) {
        File entry = directory.openNextFile();
        while (entry) {
            const String name = basenameOf(entry.path());
            if (!entry.isDirectory() && isSafeFileName(name)) {
                totalFiles++;
                if (listed < kMaximumListedFiles) {
                    JsonObject item = files.add<JsonObject>();
                    item["name"] = name;
                    item["sizeBytes"] = static_cast<uint64_t>(entry.size());
                    item["active"] = active && String(entry.path()) == currentPath;
                    item["tailComplete"] = fileHasCompleteTail(String(entry.path()));
                    listed++;
                }
            }
            entry.close();
            entry = directory.openNextFile();
        }
        directory.close();
    }
    document["count"] = totalFiles;
    document["listed"] = listed;
    document["truncated"] = totalFiles > listed;
    String output;
    serializeJson(document, output);
    return output;
}

String heltecFieldLoggerDownloadPath(const String &fileName) {
    if (!isSafeFileName(fileName)) return "";
    const String path = String(kDirectory) + "/" + fileName;
    return LittleFS.exists(path) ? path : "";
}

bool heltecFieldLoggerIsActive() {
    LoggerLock lock(pdMS_TO_TICKS(100));
    return lock && active;
}

bool heltecFieldLoggerUsesGps() {
    LoggerLock lock(pdMS_TO_TICKS(100));
    return lock && active && gpsEnabled;
}
