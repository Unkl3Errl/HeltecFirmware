#ifndef BRUCE_MULTIPLEX_SERIAL_DEVICE_H
#define BRUCE_MULTIPLEX_SERIAL_DEVICE_H

#include "SerialDevice.h"

/**
 * Keeps USB and Bluetooth command inputs available at the same time.
 *
 * Replies are written back to the transport that supplied the most recent command. USB is
 * checked first so a connected cable remains the preferred path while Bluetooth stays ready as
 * a fallback. The serial command task is the sole reader, so one active response target is
 * sufficient and avoids duplicating command output across both transports.
 */
class MultiplexSerialDevice : public SerialDevice {
public:
    void setDevices(SerialDevice *primaryDevice, SerialDevice *secondaryDevice) {
        primary = primaryDevice;
        secondary = secondaryDevice;
        active = primary ? primary : secondary;
    }

    size_t println(const String &s) override {
        SerialDevice *device = output();
        return device ? device->println(s) : 0;
    }
    size_t print(const String &s) override {
        SerialDevice *device = output();
        return device ? device->print(s) : 0;
    }
    size_t print(int n, int format) override {
        SerialDevice *device = output();
        return device ? device->print(n, format) : 0;
    }
    void vprintf(const char *fmt, va_list args) override {
        SerialDevice *device = output();
        if (device) device->vprintf(fmt, args);
    }
    size_t println() override {
        SerialDevice *device = output();
        return device ? device->println() : 0;
    }
    size_t println(size_t n) override {
        SerialDevice *device = output();
        return device ? device->println(n) : 0;
    }
    size_t println(uint32_t n) override {
        SerialDevice *device = output();
        return device ? device->println(n) : 0;
    }
    size_t println(int n, int format) override {
        SerialDevice *device = output();
        return device ? device->println(n, format) : 0;
    }
    String readStringUntil(char terminator) override {
        return input() ? input()->readStringUntil(terminator) : String();
    }
    void flush() override {
        SerialDevice *device = output();
        if (device) device->flush();
    }
    int available() override {
        if (primary) {
            const int count = primary->available();
            if (count > 0) {
                active = primary;
                return count;
            }
        }
        if (secondary) {
            const int count = secondary->available();
            if (count > 0) {
                active = secondary;
                return count;
            }
        }
        return 0;
    }
    size_t write(uint8_t *str, size_t size) override {
        SerialDevice *device = output();
        return device ? device->write(str, size) : 0;
    }
    int read() override { return input() ? input()->read() : -1; }

private:
    SerialDevice *primary = nullptr;
    SerialDevice *secondary = nullptr;
    SerialDevice *active = nullptr;

    SerialDevice *input() const { return active ? active : (primary ? primary : secondary); }
    SerialDevice *output() const { return input(); }
};

#endif // BRUCE_MULTIPLEX_SERIAL_DEVICE_H
