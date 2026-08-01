#include "serialcmds.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "utils.h"
#include <globals.h>
#ifdef ARDUINO_HELTEC_WIFI_LORA_32_V4
#include "field_logger.h"
#include <ArduinoJson.h>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#endif

QueueHandle_t cmdQueue = nullptr;
QueueHandle_t rspQueue = nullptr;
TaskHandle_t serialcmdsTaskHandle;

#ifdef ARDUINO_HELTEC_WIFI_LORA_32_V4
namespace {
constexpr char kHeltecBridgePrefix[] = "@HELTEC-BRIDGE ";
uint32_t heltecBridgeGpsSequence = 0;
uint32_t heltecBridgeWifiSequence = 0;

int hexNibble(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

bool decodeBridgeValue(const String &encoded, String &decoded) {
    decoded = "";
    decoded.reserve(encoded.length());
    for (size_t index = 0; index < encoded.length(); index++) {
        const char value = encoded[index];
        if (value == '+') {
            decoded += ' ';
        } else if (value == '%') {
            if (index + 2 >= encoded.length()) return false;
            const int high = hexNibble(encoded[index + 1]);
            const int low = hexNibble(encoded[index + 2]);
            if (high < 0 || low < 0) return false;
            decoded += static_cast<char>((high << 4) | low);
            index += 2;
        } else {
            decoded += value;
        }
    }
    return true;
}

bool bridgeFormValue(const String &form, const char *key, String &decoded) {
    const String prefix = String(key) + "=";
    size_t start = 0;
    while (start <= form.length()) {
        int end = form.indexOf('&', start);
        if (end < 0) end = form.length();
        const String field = form.substring(start, end);
        if (field.startsWith(prefix)) return decodeBridgeValue(field.substring(prefix.length()), decoded);
        if (static_cast<size_t>(end) >= form.length()) break;
        start = end + 1;
    }
    return false;
}

bool bridgeSigned(const String &text, int32_t &output) {
    if (text.length() == 0) return false;
    errno = 0;
    char *end = nullptr;
    const long value = strtol(text.c_str(), &end, 10);
    if (errno == ERANGE || end == text.c_str() || !end || *end != '\0') return false;
    if (value < INT32_MIN || value > INT32_MAX) return false;
    output = static_cast<int32_t>(value);
    return true;
}

bool bridgeUnsigned64(const String &text, uint64_t &output) {
    if (text.length() == 0 || text[0] == '-') return false;
    errno = 0;
    char *end = nullptr;
    output = strtoull(text.c_str(), &end, 10);
    return errno != ERANGE && end != text.c_str() && end && *end == '\0';
}

bool bridgeFinite(const String &text, double &output) {
    if (text.length() == 0) return false;
    errno = 0;
    char *end = nullptr;
    output = strtod(text.c_str(), &end);
    return errno != ERANGE && end != text.c_str() && end && *end == '\0' && std::isfinite(output);
}

bool bridgeMac(const String &value) {
    if (value.length() != 17) return false;
    for (size_t index = 0; index < value.length(); index++) {
        if (index % 3 == 2) {
            if (value[index] != ':') return false;
        } else if (hexNibble(value[index]) < 0) {
            return false;
        }
    }
    return true;
}

void writeBridgeResponse(const String &id, bool ok, const String &payload) {
    serialDevice->print(kHeltecBridgePrefix);
    serialDevice->print(id);
    serialDevice->print(ok ? " OK " : " ERROR ");
    serialDevice->println(payload);
}

void writeBridgeError(const String &id, const char *message) {
    JsonDocument document;
    document["error"] = message;
    String output;
    serializeJson(document, output);
    writeBridgeResponse(id, false, output);
}

bool handleHeltecBridge(const String &line) {
    if (!line.startsWith(kHeltecBridgePrefix)) return false;
    const String request = line.substring(strlen(kHeltecBridgePrefix));
    const int idEnd = request.indexOf(' ');
    if (idEnd <= 0) {
        writeBridgeError("0", "missing request id or action");
        return true;
    }
    const String id = request.substring(0, idEnd);
    for (size_t index = 0; index < id.length(); index++) {
        if (!isDigit(id[index])) {
            writeBridgeError("0", "invalid request id");
            return true;
        }
    }
    const int actionEnd = request.indexOf(' ', idEnd + 1);
    const String action = actionEnd < 0 ? request.substring(idEnd + 1)
                                        : request.substring(idEnd + 1, actionEnd);
    const String form = actionEnd < 0 ? "" : request.substring(actionEnd + 1);

    if (action == "logger-status") {
        writeBridgeResponse(id, true, heltecFieldLoggerStatusJson());
        return true;
    }
    if (action == "logger-stop") {
        if (!heltecFieldLoggerStop()) writeBridgeError(id, "field logger stop failed");
        else writeBridgeResponse(id, true, heltecFieldLoggerStatusJson());
        return true;
    }
    if (action == "logger-start") {
        String value;
        const bool gps = bridgeFormValue(form, "gps", value) && (value == "1" || value == "true");
        const bool ble = bridgeFormValue(form, "ble", value) && (value == "1" || value == "true");
        const bool wifi = bridgeFormValue(form, "wifi", value) && (value == "1" || value == "true");
        const bool resume = !bridgeFormValue(form, "autoResume", value) || value == "1" || value == "true";
        if (!heltecFieldLoggerStart(gps, ble, wifi, resume)) {
            writeBridgeError(id, "field logger start failed");
        } else {
            writeBridgeResponse(id, true, heltecFieldLoggerStatusJson());
        }
        return true;
    }
    if (action == "phone-gps") {
        String latitudeText;
        String longitudeText;
        double latitude = 0.0;
        double longitude = 0.0;
        if (
            !bridgeFormValue(form, "latitude", latitudeText) ||
            !bridgeFormValue(form, "longitude", longitudeText) ||
            !bridgeFinite(latitudeText, latitude) || !bridgeFinite(longitudeText, longitude) ||
            latitude < -90.0 || latitude > 90.0 || longitude < -180.0 || longitude > 180.0
        ) {
            writeBridgeError(id, "invalid coordinates");
            return true;
        }
        HeltecFieldGpsRecord record;
        record.source = "android";
        record.latitude = latitude;
        record.longitude = longitude;
        record.uptimeMs = millis();
        record.sequence = ++heltecBridgeGpsSequence;
        String value;
        if (bridgeFormValue(form, "provider", value)) record.provider = value.substring(0, 16);
        double optionalDouble = 0.0;
        if (bridgeFormValue(form, "accuracyMeters", value) && bridgeFinite(value, optionalDouble)) {
            record.accuracyMeters = optionalDouble;
        }
        if (bridgeFormValue(form, "altitudeMeters", value) && bridgeFinite(value, optionalDouble)) {
            record.altitudeMeters = optionalDouble;
        }
        if (bridgeFormValue(form, "speedKmph", value) && bridgeFinite(value, optionalDouble)) {
            record.speedKmph = optionalDouble;
        }
        uint64_t optionalUnsigned = 0;
        if (
            bridgeFormValue(form, "sourceUnixTimeMs", value) &&
            bridgeUnsigned64(value, optionalUnsigned)
        ) {
            record.sourceUnixTimeMs = optionalUnsigned;
        }
        if (!heltecFieldLoggerRecordGps(record)) {
            writeBridgeError(id, "active field log with GPS enabled is required");
        } else {
            writeBridgeResponse(id, true, heltecFieldLoggerStatusJson());
        }
        return true;
    }
    if (action == "phone-wifi") {
        String bssid;
        String value;
        int32_t rssiDbm = 0;
        int32_t frequencyMhz = 0;
        if (
            !bridgeFormValue(form, "bssid", bssid) || !bridgeMac(bssid) ||
            !bridgeFormValue(form, "rssiDbm", value) || !bridgeSigned(value, rssiDbm) ||
            rssiDbm < -127 || rssiDbm > 20 ||
            !bridgeFormValue(form, "frequencyMhz", value) || !bridgeSigned(value, frequencyMhz) ||
            frequencyMhz < 2000 || frequencyMhz > 7200
        ) {
            writeBridgeError(id, "invalid WiFi observation");
            return true;
        }
        bssid.toUpperCase();
        HeltecFieldWifiRecord record;
        record.bssid = bssid;
        record.rssiDbm = rssiDbm;
        record.frequencyMhz = static_cast<uint32_t>(frequencyMhz);
        record.sequence = ++heltecBridgeWifiSequence;
        if (bridgeFormValue(form, "ssid", value)) record.ssid = value.substring(0, 64);
        if (bridgeFormValue(form, "capabilities", value)) {
            record.capabilities = value.substring(0, 160);
        }
        int32_t optionalSigned = 0;
        if (bridgeFormValue(form, "channelWidth", value) && bridgeSigned(value, optionalSigned)) {
            record.channelWidth = optionalSigned;
        }
        if (
            bridgeFormValue(form, "centerFrequency0Mhz", value) &&
            bridgeSigned(value, optionalSigned) && optionalSigned >= 0 && optionalSigned <= 7200
        ) {
            record.centerFrequency0Mhz = static_cast<uint32_t>(optionalSigned);
        }
        if (
            bridgeFormValue(form, "centerFrequency1Mhz", value) &&
            bridgeSigned(value, optionalSigned) && optionalSigned >= 0 && optionalSigned <= 7200
        ) {
            record.centerFrequency1Mhz = static_cast<uint32_t>(optionalSigned);
        }
        uint64_t optionalUnsigned = 0;
        if (
            bridgeFormValue(form, "scanSequence", value) &&
            bridgeUnsigned64(value, optionalUnsigned) && optionalUnsigned <= UINT32_MAX
        ) {
            record.scanSequence = static_cast<uint32_t>(optionalUnsigned);
        }
        if (
            bridgeFormValue(form, "sourceUnixTimeMs", value) &&
            bridgeUnsigned64(value, optionalUnsigned)
        ) {
            record.sourceUnixTimeMs = optionalUnsigned;
        }
        if (!heltecFieldLoggerRecordWifi(record)) {
            writeBridgeError(id, "active field log with WiFi enabled is required");
        } else {
            writeBridgeResponse(id, true, heltecFieldLoggerStatusJson());
        }
        return true;
    }

    writeBridgeError(id, "unsupported bridge action");
    return true;
}
} // namespace
#endif

struct CmdPacket {
    char text[512]; // command size
};
bool parseSerialCommand(const String &command, bool waitForResponse) {
    if (!cmdQueue || !rspQueue) {
        Serial.println("Command or response queue not initialized");
        return false;
    }
    CmdPacket packet;
    memset(&packet, 0, sizeof(packet));
    strncpy(packet.text, command.c_str(), sizeof(packet.text) - 1);

    // Enqueue the command packet for processing
    if (xQueueSend(cmdQueue, &packet, 0) != pdTRUE) {
        Serial.println("Failed to send command to queue");
        return false;
    }
    if (!waitForResponse) { return true; }
    // Wait for the response
    bool result;
    if (xQueueReceive(rspQueue, &result, pdMS_TO_TICKS(20))) { return result; }
    Serial.println("Failed to receive command response");
    return false;
}

void handleSerialCommands(SerialCli &serialCli) {
    CmdPacket packet;
    if (cmdQueue && rspQueue) {
        if (xQueueReceive(cmdQueue, &packet, 0) == pdTRUE) {
            bool result = serialCli.parse(String(packet.text));
            xQueueSend(rspQueue, &result, 0);
            Serial.println("COMMAND: " + String(packet.text));
            Serial.printf("[CLI] Result: %s\n", result ? "TRUE" : "FALSE");
        }
    }
    if (!serialDevice->available()) return;

    String cmd_str = serialDevice->readStringUntil('\n');
    Serial.println("COMMAND: " + cmd_str);
#ifdef ARDUINO_HELTEC_WIFI_LORA_32_V4
    cmd_str.trim();
    if (handleHeltecBridge(cmd_str)) {
        serialDevice->print("# ");
        return;
    }
#endif
    serialCli.parse(cmd_str);
    serialDevice->print("# "); // prompt
    backToMenu();              // forced menu redrawn
}

void _serialCmdsTaskLoop(void *pvParameters) {
    Serial.begin(115200);
    while (1) {
        handleSerialCommands(serialCli);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void startSerialCommandsHandlerTask(bool initQueues) {
    if (initQueues) {
        cmdQueue = xQueueCreate(2, sizeof(CmdPacket));
        rspQueue = xQueueCreate(2, sizeof(bool));
    }

    xTaskCreatePinnedToCore(
        _serialCmdsTaskLoop,         // Function to implement the task
        "serialcmds",                // Name of the task (any string)
        SERIAL_CMDS_TASK_STACK_SIZE, // Stack size in bytes
        NULL, // This is a pointer to the parameter that will be passed to the new task. We are not using it
              // here and therefore it is set to NULL.
        2,    // Priority of the task
        &serialcmdsTaskHandle, // Task handle (optional, can be NULL).
#if SOC_CPU_CORES_NUM > 1
        1 // Core where the task should run. By default, all your Arduino code runs on Core 1 and the Wi-Fi
          // and RF functions
#else
        0 // Core where the task should run. ESP32-C5 has only one core
#endif
    ); // (these are usually hidden from the Arduino environment) use the Core 0.
    if (!serialcmdsTaskHandle) { Serial.println("Failed to create Serial Commands Handler task"); }
}
