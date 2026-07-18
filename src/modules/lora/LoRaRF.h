
// #ifndef __LORA_MENU_H__
// #define __LORA_MENU_H__
#ifndef __LORA_RF_H__
#define __LORA_RF_H__
#if !defined(LITE_VERSION)
#include <Arduino.h>

struct LoRaRuntimeSnapshot {
    bool listening = false;
    float frequencyMHz = 0.0f;
    uint32_t packetsReceived = 0;
    int16_t lastState = 0;
    bool hasPacket = false;
    float lastRssiDbm = 0.0f;
    float lastSnrDb = 0.0f;
    char lastMessage[257] = {};
};

enum class LoRaWebTransmitResult : uint8_t {
    Ok,
    Disabled,
    NotListening,
    EmptyPayload,
    PayloadTooLong,
    InvalidPayload,
    Cooldown,
    RadioError,
};

void lorachat();
void loraconf();
bool loraWebStartReceive(float frequencyMHz);
void loraWebStopReceive();
String loraWebStatusJson();
String loraWebHistoryJson();
void loraWebClearHistory();
LoRaRuntimeSnapshot loraRuntimeSnapshot();
void loraPollReceive();
LoRaWebTransmitResult loraWebTransmit(const String &payload);
const char *loraWebTransmitResultMessage(LoRaWebTransmitResult result);
#endif
#endif
