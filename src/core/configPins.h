#pragma once

#include "pins_arduino.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <precompiler_flags.h>
#include <set>
class BruceConfigPins {
public:
    struct UARTPins {
        gpio_num_t rx = GPIO_NUM_NC;
        gpio_num_t tx = GPIO_NUM_NC;

        UARTPins() : rx(GPIO_NUM_NC), tx(GPIO_NUM_NC) {}

        UARTPins(gpio_num_t rx = GPIO_NUM_NC, gpio_num_t tx = GPIO_NUM_NC) : rx(rx), tx(tx) {}

        void fromJson(JsonObject obj) {
            rx = (gpio_num_t)(obj["rx"] | (int)GPIO_NUM_NC);
            tx = (gpio_num_t)(obj["tx"] | (int)GPIO_NUM_NC);
        }

        void toJson(JsonObject obj) const {
            obj["rx"] = rx;
            obj["tx"] = tx;
        }
    };

    struct I2CPins {
        gpio_num_t sda = GPIO_NUM_NC;
        gpio_num_t scl = GPIO_NUM_NC;

        I2CPins() : sda(GPIO_NUM_NC), scl(GPIO_NUM_NC) {}

        I2CPins(gpio_num_t sda = GPIO_NUM_NC, gpio_num_t scl = GPIO_NUM_NC) : sda(sda), scl(scl) {}

        void fromJson(JsonObject obj) {
            sda = (gpio_num_t)(obj["sda"] | (int)GPIO_NUM_NC);
            scl = (gpio_num_t)(obj["scl"] | (int)GPIO_NUM_NC);
        }

        void toJson(JsonObject obj) const {
            obj["sda"] = sda;
            obj["scl"] = scl;
        }
    };

    struct SPIPins {
        gpio_num_t sck = GPIO_NUM_NC;
        gpio_num_t miso = GPIO_NUM_NC;
        gpio_num_t mosi = GPIO_NUM_NC;
        gpio_num_t cs = GPIO_NUM_NC;
        gpio_num_t io0 = GPIO_NUM_NC;
        gpio_num_t io2 = GPIO_NUM_NC;

        SPIPins()
            : sck(GPIO_NUM_NC), miso(GPIO_NUM_NC), mosi(GPIO_NUM_NC), cs(GPIO_NUM_NC), io0(GPIO_NUM_NC),
              io2(GPIO_NUM_NC) {}

        SPIPins(
            gpio_num_t sck_val, gpio_num_t miso_val, gpio_num_t mosi_val, gpio_num_t cs_val,
            gpio_num_t io0_val = GPIO_NUM_NC, gpio_num_t io2_val = GPIO_NUM_NC
        )
            : sck(sck_val), miso(miso_val), mosi(mosi_val), cs(cs_val), io0(io0_val), io2(io2_val) {}

        void fromJson(JsonObject obj) {
            sck = (gpio_num_t)(obj["sck"] | (int)GPIO_NUM_NC);
            miso = (gpio_num_t)(obj["miso"] | (int)GPIO_NUM_NC);
            mosi = (gpio_num_t)(obj["mosi"] | (int)GPIO_NUM_NC);
            cs = (gpio_num_t)(obj["cs"] | (int)GPIO_NUM_NC);
            io0 = (gpio_num_t)(obj["io0"] | (int)GPIO_NUM_NC);
            io2 = (gpio_num_t)(obj["io2"] | (int)GPIO_NUM_NC);
        }

        void toJson(JsonObject obj) const {
            obj["sck"] = sck;
            obj["miso"] = miso;
            obj["mosi"] = mosi;
            obj["cs"] = cs;
            obj["io0"] = io0;
            obj["io2"] = io2;
        }

        bool checkConflict(uint8_t p) {
            gpio_num_t pin = (gpio_num_t)p;
            if (sck == pin || miso == pin || mosi == pin || cs == pin) return true;
            return false;
        }
    };

    const char *filepath = "/brucePins.conf";

    // SPI Buses

#ifdef SDCARD_SCK
    SPIPins SDCARD_bus = {
        (gpio_num_t)SDCARD_SCK, (gpio_num_t)SDCARD_MISO, (gpio_num_t)SDCARD_MOSI, (gpio_num_t)SDCARD_CS
    };
#else
    SPIPins SDCARD_bus;
#endif

#if !defined(LITE_VERSION)
#if defined(W5500_SCK_PIN)
    SPIPins W5500_bus = {
        (gpio_num_t)W5500_SCK_PIN,
        (gpio_num_t)W5500_MISO_PIN,
        (gpio_num_t)W5500_MOSI_PIN,
        (gpio_num_t)W5500_SS_PIN,
        (gpio_num_t)W5500_INT_PIN,
        (gpio_num_t)W5500_RST_PIN,
    };
#else
    SPIPins W5500_bus;
#endif

#ifdef LORA_SCK
    SPIPins LoRa_bus = {
        (gpio_num_t)LORA_SCK,
        (gpio_num_t)LORA_MISO,
        (gpio_num_t)LORA_MOSI,
        (gpio_num_t)LORA_CS,
        (gpio_num_t)LORA_RST,
        (gpio_num_t)LORA_DIO0
    };
#else
    SPIPins LoRa_bus;
#endif
#endif
    // I2CPins sys_i2c = {(gpio_num_t)GROVE_SDA, (gpio_num_t)GROVE_SCL};
    I2CPins i2c_bus = {(gpio_num_t)GROVE_SDA, (gpio_num_t)GROVE_SCL};
    UARTPins uart_bus = {(gpio_num_t)SERIAL_RX, (gpio_num_t)SERIAL_TX};
    UARTPins gps_bus = {(gpio_num_t)GPS_SERIAL_RX, (gpio_num_t)GPS_SERIAL_TX};

    // Screen Rotation
    int rotation = ROTATION > 1 ? 3 : 1;

    // BLE
    String bleName = String("Keyboard_" + String((uint8_t)(ESP.getEfuseMac() >> 32), HEX));

    // iButton Pin
    int iButton = 0;

    // GPS
    int gpsBaudrate = 9600;

    /////////////////////////////////////////////////////////////////////////////////////
    // Constructor
    /////////////////////////////////////////////////////////////////////////////////////
    BruceConfigPins() {};

    /////////////////////////////////////////////////////////////////////////////////////
    // Operations
    /////////////////////////////////////////////////////////////////////////////////////
    void createFile();
    void saveFile();
    void fromFile(bool checkFS = true);
    void loadFile(JsonDocument &jsonDoc, bool checkFS = true);
    void factoryReset();
    void validateConfig();
    void fromJson(JsonObject obj);
    void toJson(JsonObject obj) const;

    void setSDCardPins(SPIPins value);
#if !defined(LITE_VERSION)
    void setLoRaPins(SPIPins value);
    void setW5500Pins(SPIPins value);
#endif
    void setSpiPins(SPIPins value);
    void setI2CPins(I2CPins value);
    void setUARTPins(UARTPins value);
    void validateSpiPins(SPIPins value);
    void validateI2CPins(I2CPins value);
    void validateUARTPins(UARTPins value);

    // Screen Rotation
    void setRotation(int value);
    void validateRotationValue();
    // BLE
    void setBleName(const String name);

    // iButton
    void setiButtonPin(int value);

    // GPS
    void setGpsBaudrate(int value);
    void validateGpsBaudrateValue();
};
