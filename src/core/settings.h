#ifndef __SETTINGS_H__
#define __SETTINGS_H__

#include "config.h"
#include "configPins.h"
#include <NTPClient.h>
#include <globals.h>

void _setBrightness(uint8_t brightval) __attribute__((weak));

void setBrightness(uint8_t brightval, bool save = true);

void getBrightness();

int gsetRotation(bool set = false);

void setBrightnessMenu();

void setUIColor();

bool setCustomUIColorMenu();

void setCustomUIColorChoiceMenu(int colorType);

void setCustomUIColorSettingMenuR(int colorType);

void setCustomUIColorSettingMenuG(int colorType);

void setCustomUIColorSettingMenuB(int colorType);

void setCustomUIColorSettingMenu(
    int colorType, int rgb, std::function<uint16_t(uint16_t, int)> colorGenerator
);

void addEvilWifiMenu();

void removeEvilWifiMenu();

void setEvilEndpointCreds();

void setEvilEndpointSsid();

void setEvilAllowEndpointDisplay();

void setEvilAllowGetCreds();

void setEvilAllowSetSsid();

void setEvilPasswordMode();

void setEvilGatewayIp();

void setSleepMode();

void setDimmerTimeMenu();

void setClock();

void runClockLoop(bool showMenuHint = false);

#ifdef HAS_RGB_LED
void setLedBlinkConfig();
#endif

void setWifiStartupConfig();

void setStartupApp();

void setGpsBaudrateMenu();

void setNetworkCredsMenu();

void setBadUSBBLEMenu();
void setBadUSBBLEKeyboardLayoutMenu();
void setBadUSBBLEKeyDelayMenu();
void setBadUSBBLEShowOutputMenu();

void setSPIPinsMenu(BruceConfigPins::SPIPins &value);

void setUARTPinsMenu(BruceConfigPins::UARTPins &value);

void setI2CPinsMenu(BruceConfigPins::I2CPins &value);

void setTheme();

void setMacAddressMenu();

#if !defined(LITE_VERSION)
void enableBLEAPI();
bool isBLEAPIEnabled();
uint32_t bleApiConnectionCount();
uint8_t bleApiConnectedClients();
bool bleApiAdvertising();

bool appStoreInstalled();

void installAppStoreJS();
#endif

#endif
