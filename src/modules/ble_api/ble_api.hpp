#ifndef BLE_API_HPP
#define BLE_API_HPP
#if !defined(LITE_VERSION)
#include "MultiplexSerialDevice.h"
#include "services/BLESerialService.h"
#include "services/BatteryService.hpp"

class BLE_API {
public:
    BLE_API();
    void setup();
    void end();
    void update_mtu(uint16_t mtu);
    void noteConnection();
    uint32_t connectionCount() const;
    uint8_t connectedClients() const;
    bool advertising() const;

private:
    NimBLEServer *pServer = nullptr;
    uint32_t totalConnections = 0;
    BatteryService battery_service;
    BLESerialService serial_service;
    MultiplexSerialDevice serial_multiplexer;
};
#endif
#endif // BLE_API_HPP
