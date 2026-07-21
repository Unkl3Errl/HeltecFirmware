#ifndef __MAIN_MENU_H__
#define __MAIN_MENU_H__

#include <MenuItemInterface.h>

#include "menu_items/BleMenu.h"
#include "menu_items/ClockMenu.h"
#include "menu_items/ConfigMenu.h"
#include "menu_items/ConnectMenu.h"
#if !defined(ARDUINO_HELTEC_WIFI_LORA_32_V4) || defined(HELTEC_ENABLE_EXTERNAL_HARDWARE_MENUS)
#include "menu_items/EthernetMenu.h"
#endif
#include "menu_items/FMMenu.h"
#include "menu_items/FileMenu.h"
#include "menu_items/GpsMenu.h"
#if !defined(ARDUINO_HELTEC_WIFI_LORA_32_V4) || defined(HELTEC_ENABLE_EXTERNAL_HARDWARE_MENUS)
#include "menu_items/IRMenu.h"
#endif
#include "menu_items/LoRaMenu.h"
#if !defined(ARDUINO_HELTEC_WIFI_LORA_32_V4) || defined(HELTEC_ENABLE_EXTERNAL_HARDWARE_MENUS)
#include "menu_items/NRF24.h"
#endif
#include "menu_items/OthersMenu.h"
#if !defined(ARDUINO_HELTEC_WIFI_LORA_32_V4) || defined(HELTEC_ENABLE_EXTERNAL_HARDWARE_MENUS)
#include "menu_items/RFIDMenu.h"
#include "menu_items/RFMenu.h"
#endif
#include "menu_items/ScriptsMenu.h"
#include "menu_items/WifiMenu.h"
class MainMenu {
public:
    FileMenu fileMenu;
    BleMenu bleMenu;
    ClockMenu clockMenu;
    ConnectMenu connectMenu;
    ConfigMenu configMenu;
    FMMenu fmMenu;
    GpsMenu gpsMenu;
#if !defined(ARDUINO_HELTEC_WIFI_LORA_32_V4) || defined(HELTEC_ENABLE_EXTERNAL_HARDWARE_MENUS)
    IRMenu irMenu;
    NRF24Menu nrf24Menu;
#endif
    OthersMenu othersMenu;
#if !defined(ARDUINO_HELTEC_WIFI_LORA_32_V4) || defined(HELTEC_ENABLE_EXTERNAL_HARDWARE_MENUS)
    RFIDMenu rfidMenu;
    RFMenu rfMenu;
#endif
    ScriptsMenu scriptsMenu;
    WifiMenu wifiMenu;
#if !defined(LITE_VERSION)
    LoRaMenu loraMenu;
#if !defined(ARDUINO_HELTEC_WIFI_LORA_32_V4) || defined(HELTEC_ENABLE_EXTERNAL_HARDWARE_MENUS)
    EthernetMenu ethernetMenu;
#endif
#endif

    MainMenu();
    ~MainMenu();

    void begin(void);
    std::vector<MenuItemInterface *> getItems(void) { return _menuItems; }
    void hideAppsMenu();

private:
    int _currentIndex = 0;
    int _totalItems = 0;
    std::vector<MenuItemInterface *> _menuItems;
};
extern MainMenu mainMenu;

#endif
