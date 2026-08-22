#if !defined(LITE_VERSION)
#include "BLESerialService.h"
#include "modules/ble/ble_common.h" // bleNotifyRetry
#include <NimBLEDevice.h>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <vector>

BLESerialService::BLESerialService() : BruceBLEService() {}

BLESerialService::~BLESerialService() { end(); }

class BLESerialCallbacks : public NimBLECharacteristicCallbacks {
    BLESerialService *service;

    void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo) override {
        service->receive(pCharacteristic->getValue());
    }

public:
    explicit BLESerialCallbacks(BLESerialService *service) : service(service) {}
};

void BLESerialService::setup(NimBLEServer *pServer) {
    if (!rxMutex) rxMutex = xSemaphoreCreateMutex();
    pService = pServer->createService("4371ec0b-3d43-49f9-b731-7c72a4a7bb91");

    serial_char = pService->createCharacteristic(
        "d555ed97-bf2a-4f46-b3eb-d1fcdd7325e9", // Battery Level
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::WRITE
    );

    callbacks = new BLESerialCallbacks(this);
    serial_char->setCallbacks(callbacks);

    pService->start();
    pServer->getAdvertising()->addServiceUUID(pService->getUUID());
}

void BLESerialService::end() {
    if (serial_char) serial_char->setCallbacks(nullptr);
    delete callbacks;
    callbacks = nullptr;
    serial_char = nullptr;
    if (rxMutex) {
        if (takeRxMutex()) {
            rxBuffer.clear();
            giveRxMutex();
        }
        vSemaphoreDelete(rxMutex);
        rxMutex = nullptr;
    }
}

int BLESerialService::available() {
    if (!takeRxMutex()) return 0;
    const size_t newline = rxBuffer.find('\n');
    const int count = newline == std::string::npos ? 0 : static_cast<int>(newline + 1);
    giveRxMutex();
    return count;
}

size_t BLESerialService::println(const String &s) {
    String toSend = s + "\r\n";
    return notifyBytes(reinterpret_cast<const uint8_t *>(toSend.c_str()), toSend.length());
}

size_t BLESerialService::print(const String &s) {
    return notifyBytes(reinterpret_cast<const uint8_t *>(s.c_str()), s.length());
}

size_t BLESerialService::println(size_t n) {
    String s = String(n);
    return println(s);
}

void BLESerialService::vprintf(const char *fmt, va_list args) {
    va_list lengthArgs;
    va_copy(lengthArgs, args);
    const int length = vsnprintf(nullptr, 0, fmt, lengthArgs);
    va_end(lengthArgs);
    if (length <= 0) return;

    std::vector<char> output(static_cast<size_t>(length) + 1);
    vsnprintf(output.data(), output.size(), fmt, args);
    notifyBytes(reinterpret_cast<const uint8_t *>(output.data()), static_cast<size_t>(length));
}

String BLESerialService::readStringUntil(char terminator) {
    if (!takeRxMutex()) return String();
    const size_t end = rxBuffer.find(terminator);
    if (end == std::string::npos) {
        giveRxMutex();
        return String();
    }
    String result(rxBuffer.substr(0, end).c_str());
    rxBuffer.erase(0, end + 1);
    giveRxMutex();
    return result;
}

size_t BLESerialService::println(const uint32_t n) {
    String s = String(n);
    return println(s);
}

size_t BLESerialService::print(const int n, int format) {
    String s = String(n, format);
    return print(s);
}

size_t BLESerialService::println(const int n, int format) {
    String s = String(n, format);
    return println(s);
}

size_t BLESerialService::println() { return println(""); }

size_t BLESerialService::write(uint8_t *str, size_t size) {
    return notifyBytes(str, size);
}

int BLESerialService::read() {
    if (!takeRxMutex()) return -1;
    if (rxBuffer.empty()) {
        giveRxMutex();
        return -1;
    }
    const uint8_t first = static_cast<uint8_t>(rxBuffer.front());
    rxBuffer.erase(0, 1);
    giveRxMutex();
    return first;
}

void BLESerialService::setMTU(uint16_t mtu) { this->mtu = mtu; }

void BLESerialService::receive(const std::string &value) {
    if (value.empty() || !takeRxMutex()) return;
    constexpr size_t maxRxBytes = 32 * 1024;
    if (value.size() >= maxRxBytes) {
        rxBuffer.assign(value.end() - maxRxBytes, value.end());
    } else {
        while (rxBuffer.size() + value.size() > maxRxBytes) {
            const size_t newline = rxBuffer.find('\n');
            if (newline == std::string::npos) {
                rxBuffer.clear();
                break;
            }
            rxBuffer.erase(0, newline + 1);
        }
        rxBuffer.append(value.data(), value.size());
    }
    giveRxMutex();
}

size_t BLESerialService::notifyBytes(const uint8_t *data, size_t size) {
    if (!serial_char || !data || size == 0) return 0;
    const size_t chunkSize = mtu > 3 ? static_cast<size_t>(mtu - 3) : 20;
    size_t sent = 0;
    while (sent < size) {
        const size_t length = std::min(chunkSize, size - sent);
        if (!bleNotifyRetry(serial_char, data + sent, length)) break;
        sent += length;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return sent;
}

bool BLESerialService::takeRxMutex() {
    return rxMutex && xSemaphoreTake(rxMutex, pdMS_TO_TICKS(100)) == pdTRUE;
}

void BLESerialService::giveRxMutex() {
    if (rxMutex) xSemaphoreGive(rxMutex);
}

#endif
