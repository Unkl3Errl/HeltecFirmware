/**
 * @file startup_app.cpp
 * @author Rennan Cockles (https://github.com/rennancockles)
 * @brief Bruce startup apps
 * @version 0.1
 * @date 2024-11-20
 */

#include "startup_app.h"

#include "core/menu_items/ScriptsMenu.h"
#include "core/settings.h" // clock
#include "core/wifi/webInterface.h"
#include "core/wifi/wifi_common.h"
#include "modules/bjs_interpreter/interpreter.h"
#include "modules/gps/gps_tracker.h"
#include "modules/gps/wardriving.h"
#include "modules/pwnagotchi/pwnagotchi.h"
#include "modules/wifi/sniffer.h"
#if defined(SOC_USB_OTG_SUPPORTED) && !defined(HELTEC_ANDROID_STORAGE)
#include "core/massStorage.h"
#endif

StartupApp::StartupApp() {
#ifndef LITE_VERSION
    _startupApps["Brucegotchi"] = []() { brucegotchi_start(); };
    _startupApps["Sniffer"] = []() { sniffer_setup(); };
    _startupApps["GPS Tracker"] = []() { GPSTracker(); };
#endif
    _startupApps["Clock"] = []() { runClockLoop(); };
#if defined(SOC_USB_OTG_SUPPORTED) && !defined(HELTEC_ANDROID_STORAGE)
    _startupApps["Mass Storage"] = []() { MassStorage(); };
#endif
    _startupApps["Wardriving"] = []() { Wardriving(); };
#ifdef ARDUINO_HELTEC_WIFI_LORA_32_V4
    // The Heltec port is operated through its dedicated BruceNet AP. Avoid a
    // station scan immediately before AP startup; on the ESP32-S3 that radio
    // transition can leave the AP configured but not advertising.
    _startupApps["WebUI"] = []() { startWebUi(true); };
#else
    _startupApps["WebUI"] = []() { startWebUi(!wifiConnecttoKnownNet()); };
#endif
#if !defined(LITE_VERSION) && !defined(DISABLE_INTERPRETER)
    _startupApps["JS Interpreter"] = []() {
        FS *fs = nullptr;
        getScriptsFolder(fs);
        if (fs == nullptr) return;
        run_bjs_script_headless(*fs, bruceConfig.startupAppJSInterpreterFile);
    };
#endif
}

bool StartupApp::startApp(const String &appName) const {
    auto it = _startupApps.find(appName);
    if (it == _startupApps.end()) {
        Serial.println("Invalid startup app: " + appName);
        return false;
    }

    it->second();

    delay(200);
    tft.fillScreen(bruceConfig.bgColor);

    return true;
}

std::vector<String> StartupApp::getAppNames() const {
    std::vector<String> keys;
    for (const auto &pair : _startupApps) { keys.push_back(pair.first); }
    return keys;
}
