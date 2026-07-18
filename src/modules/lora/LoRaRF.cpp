#if !defined(LITE_VERSION)
#include "LoRaRF.h"
#include "WString.h"
#include "core/config.h"
#include "core/configPins.h"
#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <RadioLib.h>
#include <ArduinoJson.h>
#include <core/display.h>
#include <core/mykeyboard.h>
#include <core/utils.h>
#include <globals.h>
#include <vector>

extern BruceConfigPins bruceConfigPins;

bool update = false;
String msg;
String rcvmsg;
String displayName;
bool intlora = false;
// scrolling thing
std::vector<String> messages;
int scrollOffset = 0;
const int maxMessages = 19;

#define spreadingFactor 9
#define SignalBandwidth 31.25E3
#define codingRateDenominator 8
#define preambleLength 8

#ifndef LORA_WEB_TX_POWER_DBM
#define LORA_WEB_TX_POWER_DBM 2
#endif
#ifndef LORA_WEB_TX_MAX_BYTES
#define LORA_WEB_TX_MAX_BYTES 64
#endif
#ifndef LORA_WEB_TX_COOLDOWN_MS
#define LORA_WEB_TX_COOLDOWN_MS 5000
#endif
#ifndef LORA_WEB_HISTORY_MAX_PACKETS
#define LORA_WEB_HISTORY_MAX_PACKETS 8
#endif
static_assert(LORA_WEB_HISTORY_MAX_PACKETS > 0, "LoRa WebUI history must hold at least one packet");

int contentWidth = tftWidth - 20;
int yStart = 35;
int yPos = yStart;
int ySpacing = 10;
int rightColumnX = tftWidth / 2 + 10;
SPIClass *loraSpi = nullptr;
bool loraOwnsSpiBus = false;
Module *loraModule = nullptr;
SX1276 *lora1276 = nullptr;
SX1262 *lora1262 = nullptr;
volatile bool loraPacketReceived = false;
volatile bool loraInterruptEnabled = true;
float loraCurrentFrequencyMHz = 0.0f;
float loraLastRssiDbm = NAN;
float loraLastSnrDb = NAN;
uint32_t loraReceivedPacketCount = 0;
int16_t loraLastState = RADIOLIB_ERR_NONE;
bool loraHasReceivedPacket = false;
char loraLastMessage[257] = {};
bool loraHasTransmittedPacket = false;
uint32_t loraLastTransmitMs = 0;
uint32_t loraTransmittedPacketCount = 0;
int16_t loraLastTransmitState = RADIOLIB_ERR_NONE;
struct LoRaPacketHistoryEntry {
    uint32_t sequence = 0;
    uint32_t receivedAtMs = 0;
    float rssiDbm = NAN;
    float snrDb = NAN;
    char message[257] = {};
};
LoRaPacketHistoryEntry loraPacketHistory[LORA_WEB_HISTORY_MAX_PACKETS] = {};
size_t loraPacketHistoryCount = 0;
size_t loraPacketHistoryNext = 0;
uint32_t loraPacketHistorySequence = 0;
portMUX_TYPE loraRuntimeMux = portMUX_INITIALIZER_UNLOCKED;
enum class LoRaRadioVariant { SX1276, SX1262 };
#ifdef LORA_DEFAULT_SX1262
LoRaRadioVariant loraRadioVariant = LoRaRadioVariant::SX1262;
#else
LoRaRadioVariant loraRadioVariant = LoRaRadioVariant::SX1276;
#endif

const char *defaultLoraRadioName() {
#ifdef LORA_DEFAULT_SX1262
    return "SX1262";
#else
    return "SX1276";
#endif
}

double defaultLoraFrequencyHz() {
#ifdef LORA_DEFAULT_FREQUENCY_HZ
    return static_cast<double>(LORA_DEFAULT_FREQUENCY_HZ);
#else
    return 434500000.0;
#endif
}

float configuredLoraFrequencyMHz() {
    if (loraCurrentFrequencyMHz > 0.0f) return loraCurrentFrequencyMHz;
    if (LittleFS.exists("/lora_settings.json")) {
        File file = LittleFS.open("/lora_settings.json", "r");
        JsonDocument doc;
        double stored = 0.0;
        if (file && deserializeJson(doc, file) == DeserializationError::Ok) {
            stored = doc["LoRa_Frequency"].as<String>().toDouble();
        }
        if (file) file.close();
        if (stored > 0.0) return stored > 1000.0 ? stored / 1000000.0 : stored;
    }
    return defaultLoraFrequencyHz() / 1000000.0f;
}

bool isLoraFrequencyAllowed(float frequencyMHz) {
#ifdef LORA_MIN_FREQUENCY_MHZ
    if (frequencyMHz < static_cast<float>(LORA_MIN_FREQUENCY_MHZ)) return false;
#else
    if (frequencyMHz < 150.0f) return false;
#endif
#ifdef LORA_MAX_FREQUENCY_MHZ
    if (frequencyMHz > static_cast<float>(LORA_MAX_FREQUENCY_MHZ)) return false;
#else
    if (frequencyMHz > 960.0f) return false;
#endif
    return true;
}

void saveLoraWebFrequency(float frequencyMHz) {
    JsonDocument doc;
    if (LittleFS.exists("/lora_settings.json")) {
        File input = LittleFS.open("/lora_settings.json", "r");
        if (input) {
            deserializeJson(doc, input);
            input.close();
        }
    }
    if (doc["LoRa_Name"].isNull()) doc["LoRa_Name"] = "BruceTest";
    doc["LoRa_Radio"] = defaultLoraRadioName();
    doc["LoRa_Frequency"] = String(frequencyMHz * 1000000.0f, 2);
    File output = LittleFS.open("/lora_settings.json", "w");
    if (output) {
        serializeJson(doc, output);
        output.close();
    }
}

void configureLoraFrontend() {
#ifdef LORA_FEM_POWER
    pinMode(LORA_FEM_POWER, OUTPUT);
    digitalWrite(LORA_FEM_POWER, HIGH);
#endif
#ifdef LORA_FEM_ENABLE
    pinMode(LORA_FEM_ENABLE, OUTPUT);
    digitalWrite(LORA_FEM_ENABLE, HIGH);
#endif
#ifdef LORA_FEM_TX
    pinMode(LORA_FEM_TX, OUTPUT);
    digitalWrite(LORA_FEM_TX, LOW);
#endif
    delay(2);
}

void setLoraFrontendTx(bool transmit) {
#ifdef LORA_FEM_TX
    digitalWrite(LORA_FEM_TX, transmit ? HIGH : LOW);
    delayMicroseconds(100);
#else
    (void)transmit;
#endif
}

void disableLoraFrontend() {
#ifdef LORA_FEM_TX
    pinMode(LORA_FEM_TX, OUTPUT);
    digitalWrite(LORA_FEM_TX, LOW);
#endif
#ifdef LORA_FEM_ENABLE
    pinMode(LORA_FEM_ENABLE, OUTPUT);
    digitalWrite(LORA_FEM_ENABLE, LOW);
#endif
#ifdef LORA_FEM_POWER
    pinMode(LORA_FEM_POWER, OUTPUT);
    digitalWrite(LORA_FEM_POWER, LOW);
#endif
}

int getLoraIrqPin() {
#ifdef LORA_IRQ
    return LORA_IRQ;
#else
    return bruceConfigPins.LoRa_bus.io2;
#endif
}

int getLoraBusyPin() {
#ifdef LORA_BUSY
    return LORA_BUSY;
#else
    return GPIO_NUM_NC;
#endif
}

int getLoraResetPin() { return bruceConfigPins.LoRa_bus.io0; }
int getLoraCsPin() { return bruceConfigPins.LoRa_bus.cs; }

void clearLoraRadio() {
    loraInterruptEnabled = false;
    loraPacketReceived = false;
    if (lora1276 || lora1262) {
        setLoraFrontendTx(false);
        if (lora1276) {
            lora1276->clearDio0Action();
            lora1276->sleep();
        }
        if (lora1262) {
            lora1262->clearDio1Action();
            lora1262->sleep();
        }
    }
    if (lora1276) {
        delete lora1276;
        lora1276 = nullptr;
    }
    if (lora1262) {
        delete lora1262;
        lora1262 = nullptr;
    }
    if (loraModule) {
        delete loraModule;
        loraModule = nullptr;
    }
    if (loraOwnsSpiBus && loraSpi) loraSpi->end();
    loraSpi = nullptr;
    loraOwnsSpiBus = false;
    disableLoraFrontend();
    portENTER_CRITICAL(&loraRuntimeMux);
    intlora = false;
    portEXIT_CRITICAL(&loraRuntimeMux);
}

void onLoraPacket() {
    if (!loraInterruptEnabled) return;
    loraPacketReceived = true;
}

SPIClass *selectLoraSPIBus() {
    SPIClass *selectedSPI = &SPI;
    loraOwnsSpiBus = false;
    if (bruceConfigPins.LoRa_bus.mosi == TFT_MOSI) {
#if TFT_MOSI > 0
        selectedSPI = &tft.getSPIinstance();
#endif
        Serial.println("Using TFT SPI for LoRa");
    } else if (bruceConfigPins.SDCARD_bus.mosi == bruceConfigPins.LoRa_bus.mosi) {
        selectedSPI = &sdcardSPI;
        Serial.println("Using SDCard SPI for LoRa");
    } else if (
        bruceConfigPins.NRF24_bus.mosi == bruceConfigPins.LoRa_bus.mosi ||
        bruceConfigPins.CC1101_bus.mosi == bruceConfigPins.LoRa_bus.mosi
    ) {
        selectedSPI = &CC_NRF_SPI;
        CC_NRF_SPI.begin(
            (int8_t)bruceConfigPins.LoRa_bus.sck,
            (int8_t)bruceConfigPins.LoRa_bus.miso,
            (int8_t)bruceConfigPins.LoRa_bus.mosi
        );
        Serial.println("Using CC/NRF SPI for LoRa");
    } else {
        SPI.begin(
            bruceConfigPins.LoRa_bus.sck,
            bruceConfigPins.LoRa_bus.miso,
            bruceConfigPins.LoRa_bus.mosi,
            bruceConfigPins.LoRa_bus.cs
        );
        loraOwnsSpiBus = true;
        Serial.println("Using dedicated SPI for LoRa");
    }
    return selectedSPI;
}

bool startLoraRadio(float bandMHz) {
    clearLoraRadio();
    loraPacketReceived = false;
    loraInterruptEnabled = true;
    const int irqPin = getLoraIrqPin();
    if (getLoraCsPin() == GPIO_NUM_NC || bruceConfigPins.LoRa_bus.mosi == GPIO_NUM_NC ||
        bruceConfigPins.LoRa_bus.miso == GPIO_NUM_NC || bruceConfigPins.LoRa_bus.sck == GPIO_NUM_NC) {
        Serial.println("LoRa pins not configured!");
        displayError("LoRa pins not configured!", true);
        return false;
    }
    if (irqPin == GPIO_NUM_NC) {
        Serial.println("LoRa IRQ pin not configured!");
        displayError("LoRa IRQ pin not configured!", true);
        return false;
    }

    configureLoraFrontend();
    setLoraFrontendTx(false);
    loraSpi = selectLoraSPIBus();
    const int busyPin = (loraRadioVariant == LoRaRadioVariant::SX1262) ? getLoraBusyPin() : GPIO_NUM_NC;
    if (loraRadioVariant == LoRaRadioVariant::SX1262 && busyPin == GPIO_NUM_NC) {
        Serial.println("Warning: SX1262 selected but BUSY pin is not configured");
    }
    loraModule = new Module(getLoraCsPin(), irqPin, getLoraResetPin(), busyPin, *loraSpi);

    int state = RADIOLIB_ERR_NONE;
    if (loraRadioVariant == LoRaRadioVariant::SX1276) {
        lora1276 = new SX1276(loraModule);
        state = lora1276->begin(bandMHz);
        if (state == RADIOLIB_ERR_NONE) { lora1276->setDio0Action(onLoraPacket, CHANGE); }
        if (state == RADIOLIB_ERR_NONE) state = lora1276->setSpreadingFactor(spreadingFactor);
        if (state == RADIOLIB_ERR_NONE) state = lora1276->setBandwidth(SignalBandwidth / 1000.0);
        if (state == RADIOLIB_ERR_NONE) state = lora1276->setCodingRate(codingRateDenominator);
        if (state == RADIOLIB_ERR_NONE) state = lora1276->setPreambleLength(preambleLength);
        if (state == RADIOLIB_ERR_NONE) state = lora1276->startReceive();
    } else {
        lora1262 = new SX1262(loraModule);
        state = lora1262->begin(bandMHz);
        if (state == RADIOLIB_ERR_NONE) { lora1262->setDio1Action(onLoraPacket); }
        if (state == RADIOLIB_ERR_NONE) state = lora1262->setSpreadingFactor(spreadingFactor);
        if (state == RADIOLIB_ERR_NONE) state = lora1262->setBandwidth(SignalBandwidth / 1000.0);
        if (state == RADIOLIB_ERR_NONE) state = lora1262->setCodingRate(codingRateDenominator);
        if (state == RADIOLIB_ERR_NONE) state = lora1262->setPreambleLength(preambleLength);
        if (state == RADIOLIB_ERR_NONE) state = lora1262->startReceive();
    }

    if (state != RADIOLIB_ERR_NONE) {
        portENTER_CRITICAL(&loraRuntimeMux);
        loraLastState = state;
        portEXIT_CRITICAL(&loraRuntimeMux);
        Serial.printf("Starting LoRa failed! Err %d\n", state);
        displayError("LoRa Init Failed", true);
        clearLoraRadio();
        return false;
    }
    portENTER_CRITICAL(&loraRuntimeMux);
    intlora = true;
    loraCurrentFrequencyMHz = bandMHz;
    loraLastState = RADIOLIB_ERR_NONE;
    portEXIT_CRITICAL(&loraRuntimeMux);
    Serial.println("LoRa Started");
    return true;
}

bool sendLoraMessage(String &payload) {
    if (!intlora) return false;
    loraInterruptEnabled = false;
    setLoraFrontendTx(true);
    int transmitState = RADIOLIB_ERR_NONE;
    if (loraRadioVariant == LoRaRadioVariant::SX1276 && lora1276) {
        transmitState = lora1276->transmit(payload);
    } else if (loraRadioVariant == LoRaRadioVariant::SX1262 && lora1262) {
        transmitState = lora1262->transmit(payload);
    } else {
        setLoraFrontendTx(false);
        loraInterruptEnabled = true;
        return false;
    }

    // Return the V4 antenna switch to RX before asking the radio to listen.
    setLoraFrontendTx(false);
    int receiveState = (loraRadioVariant == LoRaRadioVariant::SX1262 && lora1262)
                           ? lora1262->startReceive()
                           : lora1276->startReceive();
    loraInterruptEnabled = true;
    portENTER_CRITICAL(&loraRuntimeMux);
    loraHasTransmittedPacket = true;
    loraLastTransmitMs = millis();
    loraLastTransmitState = transmitState != RADIOLIB_ERR_NONE ? transmitState : receiveState;
    if (transmitState == RADIOLIB_ERR_NONE) loraTransmittedPacketCount++;
    portEXIT_CRITICAL(&loraRuntimeMux);
    if (transmitState != RADIOLIB_ERR_NONE) {
        Serial.printf("LoRa transmit failed: %d\n", transmitState);
        displayError("LoRa send failed");
        return false;
    }
    if (receiveState != RADIOLIB_ERR_NONE) {
        Serial.printf("LoRa receive restart failed: %d\n", receiveState);
        displayError("LoRa RX restart failed");
        return false;
    }
    return true;
}

void reciveMessage() {
    if (!loraPacketReceived || !intlora) return;
    loraInterruptEnabled = false;
    loraPacketReceived = false;
    String incoming;
    int state = (loraRadioVariant == LoRaRadioVariant::SX1262 && lora1262)
                    ? lora1262->readData(incoming)
                    : (lora1276 ? lora1276->readData(incoming) : -1);
    if (state == RADIOLIB_ERR_NONE) {
        rcvmsg = incoming;
        char messagePreview[sizeof(loraLastMessage)] = {};
        const size_t previewLength = min(static_cast<size_t>(incoming.length()), sizeof(messagePreview) - 1);
        for (size_t i = 0; i < previewLength; i++) {
            const uint8_t value = static_cast<uint8_t>(incoming[i]);
            messagePreview[i] = value >= 32 && value <= 126 ? static_cast<char>(value) : '.';
        }
        float receivedRssiDbm = NAN;
        float receivedSnrDb = NAN;
        if (loraRadioVariant == LoRaRadioVariant::SX1262 && lora1262) {
            receivedRssiDbm = lora1262->getRSSI();
            receivedSnrDb = lora1262->getSNR();
        } else if (lora1276) {
            receivedRssiDbm = lora1276->getRSSI();
            receivedSnrDb = lora1276->getSNR();
        }
        const uint32_t receivedAtMs = millis();
        portENTER_CRITICAL(&loraRuntimeMux);
        loraReceivedPacketCount++;
        loraLastRssiDbm = receivedRssiDbm;
        loraLastSnrDb = receivedSnrDb;
        loraHasReceivedPacket = true;
        memcpy(loraLastMessage, messagePreview, sizeof(loraLastMessage));
        LoRaPacketHistoryEntry &historyEntry = loraPacketHistory[loraPacketHistoryNext];
        historyEntry.sequence = ++loraPacketHistorySequence;
        historyEntry.receivedAtMs = receivedAtMs;
        historyEntry.rssiDbm = receivedRssiDbm;
        historyEntry.snrDb = receivedSnrDb;
        memcpy(historyEntry.message, messagePreview, sizeof(historyEntry.message));
        loraPacketHistoryNext = (loraPacketHistoryNext + 1) % LORA_WEB_HISTORY_MAX_PACKETS;
        if (loraPacketHistoryCount < LORA_WEB_HISTORY_MAX_PACKETS) loraPacketHistoryCount++;
        portEXIT_CRITICAL(&loraRuntimeMux);
        Serial.println("Recived:" + rcvmsg);
        File file = LittleFS.open("/chats.txt", "a");
        file.println(rcvmsg);
        file.close();
        messages.push_back(rcvmsg);
        if (messages.size() > maxMessages) { scrollOffset = messages.size() - maxMessages; }
        update = true;
    } else {
        Serial.printf("LoRa read failed: %d\n", state);
    }
    if (loraRadioVariant == LoRaRadioVariant::SX1262 && lora1262) {
        lora1262->startReceive();
    } else if (lora1276) {
        lora1276->startReceive();
    }
    loraInterruptEnabled = true;
}

bool loraWebStartReceive(float frequencyMHz) {
    if (!isLoraFrequencyAllowed(frequencyMHz)) {
        portENTER_CRITICAL(&loraRuntimeMux);
        loraLastState = RADIOLIB_ERR_INVALID_FREQUENCY;
        portEXIT_CRITICAL(&loraRuntimeMux);
        return false;
    }
#ifdef LORA_DEFAULT_SX1262
    loraRadioVariant = LoRaRadioVariant::SX1262;
#endif
    const LoRaRuntimeSnapshot current = loraRuntimeSnapshot();
    if (current.listening && fabsf(current.frequencyMHz - frequencyMHz) < 0.0005f) return true;
    if (!startLoraRadio(frequencyMHz)) return false;
    saveLoraWebFrequency(frequencyMHz);
    return true;
}

void loraWebStopReceive() { clearLoraRadio(); }

LoRaRuntimeSnapshot loraRuntimeSnapshot() {
    LoRaRuntimeSnapshot snapshot;
    portENTER_CRITICAL(&loraRuntimeMux);
    snapshot.listening = intlora;
    snapshot.frequencyMHz = loraCurrentFrequencyMHz;
    snapshot.packetsReceived = loraReceivedPacketCount;
    snapshot.lastState = loraLastState;
    snapshot.hasPacket = loraHasReceivedPacket;
    snapshot.lastRssiDbm = loraLastRssiDbm;
    snapshot.lastSnrDb = loraLastSnrDb;
    memcpy(snapshot.lastMessage, loraLastMessage, sizeof(snapshot.lastMessage));
    portEXIT_CRITICAL(&loraRuntimeMux);
    return snapshot;
}

void loraPollReceive() { reciveMessage(); }

String loraWebHistoryJson() {
    size_t count = 0;
    size_t next = 0;
    portENTER_CRITICAL(&loraRuntimeMux);
    count = loraPacketHistoryCount;
    next = loraPacketHistoryNext;
    portEXIT_CRITICAL(&loraRuntimeMux);

    const uint32_t now = millis();
    JsonDocument doc;
    doc["capacity"] = LORA_WEB_HISTORY_MAX_PACKETS;
    doc["count"] = count;
    doc["uptimeMs"] = now;
    JsonArray packets = doc["packets"].to<JsonArray>();
    const size_t oldest = (next + LORA_WEB_HISTORY_MAX_PACKETS - count) % LORA_WEB_HISTORY_MAX_PACKETS;
    for (size_t offset = 0; offset < count; offset++) {
        LoRaPacketHistoryEntry entry;
        portENTER_CRITICAL(&loraRuntimeMux);
        entry = loraPacketHistory[(oldest + offset) % LORA_WEB_HISTORY_MAX_PACKETS];
        portEXIT_CRITICAL(&loraRuntimeMux);
        JsonObject packet = packets.add<JsonObject>();
        packet["sequence"] = entry.sequence;
        packet["receivedAtMs"] = entry.receivedAtMs;
        packet["ageMs"] = now - entry.receivedAtMs;
        packet["message"] = entry.message;
        if (!isnan(entry.rssiDbm)) packet["rssiDbm"] = entry.rssiDbm;
        if (!isnan(entry.snrDb)) packet["snrDb"] = entry.snrDb;
    }

    String output;
    serializeJson(doc, output);
    return output;
}

void loraWebClearHistory() {
    portENTER_CRITICAL(&loraRuntimeMux);
    memset(loraPacketHistory, 0, sizeof(loraPacketHistory));
    loraPacketHistoryCount = 0;
    loraPacketHistoryNext = 0;
    loraPacketHistorySequence = 0;
    portEXIT_CRITICAL(&loraRuntimeMux);
}

uint32_t loraWebTransmitCooldownRemainingMs() {
    uint32_t lastTransmitMs = 0;
    bool hasTransmitted = false;
    portENTER_CRITICAL(&loraRuntimeMux);
    lastTransmitMs = loraLastTransmitMs;
    hasTransmitted = loraHasTransmittedPacket;
    portEXIT_CRITICAL(&loraRuntimeMux);
    if (!hasTransmitted) return 0;
    const uint32_t elapsed = millis() - lastTransmitMs;
    return elapsed >= LORA_WEB_TX_COOLDOWN_MS ? 0 : LORA_WEB_TX_COOLDOWN_MS - elapsed;
}

LoRaWebTransmitResult loraWebTransmit(const String &payload) {
#ifndef LORA_WEB_TX_ENABLED
    (void)payload;
    return LoRaWebTransmitResult::Disabled;
#else
    if (!loraRuntimeSnapshot().listening) return LoRaWebTransmitResult::NotListening;
    if (payload.isEmpty()) return LoRaWebTransmitResult::EmptyPayload;
    if (payload.length() > LORA_WEB_TX_MAX_BYTES) return LoRaWebTransmitResult::PayloadTooLong;
    for (size_t i = 0; i < payload.length(); i++) {
        const uint8_t value = static_cast<uint8_t>(payload[i]);
        if (value < 32 || value > 126) return LoRaWebTransmitResult::InvalidPayload;
    }
    if (loraWebTransmitCooldownRemainingMs() > 0) return LoRaWebTransmitResult::Cooldown;

    int16_t state = RADIOLIB_ERR_UNKNOWN;
    if (loraRadioVariant == LoRaRadioVariant::SX1262 && lora1262) {
        state = lora1262->setOutputPower(LORA_WEB_TX_POWER_DBM);
    } else if (loraRadioVariant == LoRaRadioVariant::SX1276 && lora1276) {
        state = lora1276->setOutputPower(LORA_WEB_TX_POWER_DBM);
    }
    if (state != RADIOLIB_ERR_NONE) {
        portENTER_CRITICAL(&loraRuntimeMux);
        loraLastTransmitState = state;
        portEXIT_CRITICAL(&loraRuntimeMux);
        return LoRaWebTransmitResult::RadioError;
    }

    const float frequencyMHz = loraRuntimeSnapshot().frequencyMHz;
    String outgoing = payload;
    const bool sent = sendLoraMessage(outgoing);
    Serial.printf(
        "[LoRa] Web TX %u bytes at %.3f MHz / %d dBm: %s\n",
        (unsigned)payload.length(),
        frequencyMHz,
        LORA_WEB_TX_POWER_DBM,
        sent ? "ok" : "failed"
    );
    return sent ? LoRaWebTransmitResult::Ok : LoRaWebTransmitResult::RadioError;
#endif
}

const char *loraWebTransmitResultMessage(LoRaWebTransmitResult result) {
    switch (result) {
        case LoRaWebTransmitResult::Ok: return "transmitted";
        case LoRaWebTransmitResult::Disabled: return "transmission is disabled on this build";
        case LoRaWebTransmitResult::NotListening: return "start the receiver before transmitting";
        case LoRaWebTransmitResult::EmptyPayload: return "payload is empty";
        case LoRaWebTransmitResult::PayloadTooLong: return "payload exceeds the configured limit";
        case LoRaWebTransmitResult::InvalidPayload: return "payload must contain printable ASCII only";
        case LoRaWebTransmitResult::Cooldown: return "transmit cooldown is active";
        case LoRaWebTransmitResult::RadioError: return "radio transmission failed";
    }
    return "unknown transmit result";
}

String loraWebStatusJson() {
    loraPollReceive();
    const LoRaRuntimeSnapshot runtime = loraRuntimeSnapshot();
    JsonDocument doc;
    doc["available"] =
        getLoraCsPin() != GPIO_NUM_NC && bruceConfigPins.LoRa_bus.mosi != GPIO_NUM_NC &&
        bruceConfigPins.LoRa_bus.miso != GPIO_NUM_NC && bruceConfigPins.LoRa_bus.sck != GPIO_NUM_NC &&
        getLoraIrqPin() != GPIO_NUM_NC;
    doc["radio"] = (loraRadioVariant == LoRaRadioVariant::SX1262) ? "SX1262" : "SX1276";
    doc["listening"] = runtime.listening;
    doc["frequencyMHz"] =
        runtime.frequencyMHz > 0.0f ? runtime.frequencyMHz : configuredLoraFrequencyMHz();
    doc["lastState"] = runtime.lastState;
    doc["packetsReceived"] = runtime.packetsReceived;
    doc["lastMessage"] = runtime.lastMessage;
    size_t historyCount = 0;
    portENTER_CRITICAL(&loraRuntimeMux);
    historyCount = loraPacketHistoryCount;
    portEXIT_CRITICAL(&loraRuntimeMux);
    doc["historyCount"] = historyCount;
    doc["historyCapacity"] = LORA_WEB_HISTORY_MAX_PACKETS;
    if (runtime.hasPacket && !isnan(runtime.lastRssiDbm)) doc["rssiDbm"] = runtime.lastRssiDbm;
    if (runtime.hasPacket && !isnan(runtime.lastSnrDb)) doc["snrDb"] = runtime.lastSnrDb;
#ifdef LORA_MIN_FREQUENCY_MHZ
    doc["minimumFrequencyMHz"] = static_cast<float>(LORA_MIN_FREQUENCY_MHZ);
#endif
#ifdef LORA_MAX_FREQUENCY_MHZ
    doc["maximumFrequencyMHz"] = static_cast<float>(LORA_MAX_FREQUENCY_MHZ);
#endif
#ifdef LORA_WEB_TX_ENABLED
    uint32_t transmittedPackets = 0;
    int16_t lastTransmitState = RADIOLIB_ERR_NONE;
    portENTER_CRITICAL(&loraRuntimeMux);
    transmittedPackets = loraTransmittedPacketCount;
    lastTransmitState = loraLastTransmitState;
    portEXIT_CRITICAL(&loraRuntimeMux);
    doc["transmitAvailable"] = true;
    doc["transmitPowerDbm"] = LORA_WEB_TX_POWER_DBM;
    doc["maximumTransmitBytes"] = LORA_WEB_TX_MAX_BYTES;
    doc["transmitCooldownMs"] = LORA_WEB_TX_COOLDOWN_MS;
    doc["transmitCooldownRemainingMs"] = loraWebTransmitCooldownRemainingMs();
    doc["transmittedPackets"] = transmittedPackets;
    doc["lastTransmitState"] = lastTransmitState;
#else
    doc["transmitAvailable"] = false;
#endif
    String output;
    serializeJson(doc, output);
    return output;
}

// render stuff

void render() {
    if (!update) return;
    tft.setTextSize(1);
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(0x6DFC);
    if (!intlora) { tft.drawString("Lora Init Failed", 10, 13); }
    Serial.println(String(displayName));
    tft.drawString("USRN: " + String(displayName), 10, 25);

    int yPos = yStart;
    int endLine = scrollOffset + maxMessages;
    if (endLine > messages.size()) endLine = messages.size();
    for (int i = scrollOffset; i < endLine; i++) {
        tft.setTextColor(bruceConfig.priColor);
        tft.drawString(messages[i], 10, yPos);
        yPos += ySpacing;
    }
    update = false;
}

void loadMessages() {
    messages.clear();
    File file = LittleFS.open("/chats.txt", "r");
    while (file.available()) {
        String line = file.readStringUntil('\n');
        messages.push_back(line);
    }
    file.close();
    if (messages.size() > maxMessages) {
        scrollOffset = messages.size() - maxMessages;
    } else {
        scrollOffset = 0;
    }
}

// optional call funcs
void sendmsg() {
    Serial.println("C bttn");
    tft.fillScreen(TFT_BLACK);
    if (!intlora) {
        tft.setTextColor(bruceConfig.priColor);

        tft.setTextColor(TFT_RED);
        tft.setTextSize(2);
        tft.setCursor(10, tftHeight / 2 - 10);
        tft.print("LoRa not init!");

        tft.drawCentreString("LoRa not initialized!", tftWidth / 2, tftHeight / 2, 2);
        delay(1500);
        update = true;
        return;
    }
    msg = keyboard(msg, 256, "Message:");
    if (msg == "\x1B") return;
    msg = String(displayName) + ": " + msg;
    if (msg == "") return;
    Serial.println(msg);
    if (!sendLoraMessage(msg)) {
        update = true;
        return;
    }
    tft.fillScreen(TFT_BLACK);
    update = true;
    File file = LittleFS.open("/chats.txt", "a");
    file.println(msg);
    file.close();

    messages.push_back(msg);
    if (messages.size() > maxMessages) { scrollOffset = messages.size() - maxMessages; }
    msg = "";
}

void upress() {
    Serial.println("Up Pressed");
    if (scrollOffset > 0) {
        scrollOffset--;
        update = true;
    }
}

void downpress() {
    Serial.println("Down Pressed");
    if (scrollOffset < messages.size() - maxMessages) {
        scrollOffset++;
        update = true;
    }
}

void selectRadioVariant(JsonDocument &doc) {
    String stored = doc["LoRa_Radio"] | defaultLoraRadioName();
    if (stored.equalsIgnoreCase("SX1262")) { loraRadioVariant = LoRaRadioVariant::SX1262; }
    std::vector<Option> radioOptions = {
        {"SX1276", []() {}},
        {"SX1262", []() {}}
    };
    int selected = loopOptions(
        radioOptions, MENU_TYPE_SUBMENU, "LoRa Radio", (loraRadioVariant == LoRaRadioVariant::SX1262) ? 1 : 0
    );
    if (selected >= 0) {
        loraRadioVariant = (selected == 1) ? LoRaRadioVariant::SX1262 : LoRaRadioVariant::SX1276;
        doc["LoRa_Radio"] = (loraRadioVariant == LoRaRadioVariant::SX1262) ? "SX1262" : "SX1276";
        File cfg = LittleFS.open("/lora_settings.json", "w");
        serializeJson(doc, cfg);
        cfg.close();
    }
}

void mainloop() {
    long pressStartTime = 0;
    bool isPressing = false;
    bool breakloop = false;
    while (true) {
        render();
        reciveMessage();
        if (breakloop) { break; }
#ifdef HAS_3_BUTTONS
        if (EscPress) {
            long _tmp = millis();

            LongPress = true;
            while (EscPress) {
                if (millis() - _tmp > 200) {
                    // start drawing arc after short delay; animate over 500ms
                    int sweep = 0;
                    long elapsed = millis() - (_tmp + 200);
                    if (elapsed > 0) sweep = 360 * elapsed / 500;
                    if (sweep > 360) sweep = 360;
                    tft.drawArc(
                        tftWidth / 2,
                        tftHeight / 2,
                        25,
                        15,
                        0,
                        sweep,
                        getColorVariation(bruceConfig.priColor),
                        bruceConfig.bgColor
                    );
                }
                vTaskDelay(10 / portTICK_PERIOD_MS);
            }
            // clear arc
            tft.drawArc(
                tftWidth / 2, tftHeight / 2, 25, 15, 0, 360, bruceConfig.bgColor, bruceConfig.bgColor
            );
            LongPress = false;
            // #endif

            // decide short vs long after release
            if (millis() - _tmp > 700) {
                // long press -> exit
                breakloop = true;
            } else {
                // short press -> scroll down; consume flag first
                check(EscPress);
                upress();
            }
        }

        if (check(NextPress)) downpress();
        if (check(SelPress)) sendmsg();
#else

        if (check(NextPress)) downpress();
        if (check(EscPress)) break;
        if (check(PrevPress)) upress();
        if (check(SelPress)) sendmsg();
#endif

        delay(20);
    }
}

void lorachat() {
    // set filesystem thing
    if (!LittleFS.exists("/chats.txt")) {
        File file = LittleFS.open("/chats.txt", "w");
        file.close();
        Serial.println("chat file created :)");
    }
    if (!LittleFS.exists("/lora_settings.json")) {
        Serial.println("creating lora settings .json file");
        JsonDocument doc;
        File file = LittleFS.open("/lora_settings.json", "w");
        doc["LoRa_Frequency"] = String(defaultLoraFrequencyHz(), 2);
        doc["LoRa_Name"] = "BruceTest";
        doc["LoRa_Radio"] = defaultLoraRadioName();
        serializeJson(doc, file);
        file.close();
    }
    File file = LittleFS.open("/lora_settings.json", "r");
    JsonDocument doc;
    deserializeJson(doc, file);
    double BAND = doc["LoRa_Frequency"].as<String>().toDouble();
    file.close();

#ifdef LORA_DEFAULT_FREQUENCY_HZ
    // Migrate only Bruce's untouched legacy default. User-selected values are
    // preserved, including values saved in MHz rather than Hz.
    if (BAND == 434500000.0 && defaultLoraFrequencyHz() != 434500000.0) {
        BAND = defaultLoraFrequencyHz();
        doc["LoRa_Frequency"] = String(BAND, 2);
        File cfg = LittleFS.open("/lora_settings.json", "w");
        serializeJson(doc, cfg);
        cfg.close();
        Serial.printf("Migrated LoRa default frequency to %.3f MHz\n", BAND / 1000000.0);
    }
#endif
    displayName = doc["LoRa_Name"].as<String>();
    selectRadioVariant(doc);
    float bandMHz = (BAND > 1000) ? BAND / 1000000.0f : BAND;
    if (bandMHz <= 0) {
        displayError("Invalid LoRa frequency", true);
        return;
    }
    tft.fillScreen(TFT_BLACK);
    update = true;
    Serial.println("Initializing LoRa...");
    Serial.println(
        "Pins: SCK:" + String(bruceConfigPins.LoRa_bus.sck) +
        " MISO:" + String(bruceConfigPins.LoRa_bus.miso) + " MOSI:" + String(bruceConfigPins.LoRa_bus.mosi) +
        " CS:" + String(bruceConfigPins.LoRa_bus.cs) + " RST:" + String(getLoraResetPin()) +
        " IRQ:" + String(getLoraIrqPin()) + "BAND: " + String(bandMHz) +
        "MHz Radio: " + ((loraRadioVariant == LoRaRadioVariant::SX1262) ? "SX1262" : "SX1276") +
        " DisplayName:  " + displayName
    );

    if (!startLoraRadio(bandMHz)) {
        update = true;
        return;
    }
    tft.setTextWrap(true, true);
    tft.setTextDatum(TL_DATUM);
    loadMessages();
    mainloop();
    clearLoraRadio();
}

// settings
// check the saving and loading
void changeusername() {
    tft.fillScreen(TFT_BLACK);
    String username = keyboard(username, 64, "");
    if (username == "" || username == "\x1B") return;
    File file = LittleFS.open("/lora_settings.json", "r");
    JsonDocument doc;
    deserializeJson(doc, file);
    file.close();
    doc["LoRa_Name"] = username;
    file = LittleFS.open("/lora_settings.json", "w");
    serializeJson(doc, file);
    file.close();
}

void chfreq() {
    tft.fillScreen(TFT_BLACK);
    char buf[15];
    File file = LittleFS.open("/lora_settings.json", "r");
    JsonDocument doc;
    deserializeJson(doc, file);
    file.close();

    double dfreq = doc["LoRa_Frequency"].as<String>().toDouble();
    dfreq = dfreq / 1000000;
    snprintf(buf, sizeof(buf), "%.3f", dfreq);
    String freq = num_keyboard(buf, 12, "in Mhz");
    dfreq = freq.toDouble();
    if (dfreq == 0 || freq == "\x1B") {
        displayError("Invalid value");
        return;
    } else if (dfreq > 1000) {
        displayError("Invalid value, Exceeds 1Ghz");
        return;
    }
    dfreq = dfreq * 1000000;
    snprintf(buf, sizeof(buf), "%.2f", dfreq);
    doc["LoRa_Frequency"] = buf;

    file = LittleFS.open("/lora_settings.json", "w");
    serializeJson(doc, file);
    file.close();
}
#endif
