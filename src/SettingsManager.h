#ifndef SETTINGS_MANAGER_H
#define SETTINGS_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <vector>

struct WiFiNetwork {
    String ssid;
    String password;
};

struct SettingsData {
    // Preferred spelling. wifi_ssd is retained below only for backwards
    // compatibility with settings.json files created by earlier builds.
    String wifi_ssid;
    String wifi_ssd;
    String wifi_pass;

    String weather_api_key;
    String tide_api_key;
    String location_name;
    String weather_lat;
    String weather_long;
    int32_t timezone_offset_seconds;

    unsigned long lastUpdate;
    uint16_t brightness_level;
    uint16_t screen_dim_duration;
    uint16_t sleep_duration;
    uint16_t system_volume;

    std::vector<WiFiNetwork> known_wifi_networks;
};

extern SettingsData currentSettings;

bool loadSettingsDataFromFile(const char* filePath, SettingsData& settings);
void saveSettingsDataToFile(const char* filePath, const SettingsData& settings);
void initializeSettingsData();

int findKnownWiFiNetworkIndex(const String& ssid);
bool addOrUpdateKnownWiFiNetwork(const String& ssid, const String& password, bool makeFirst = true);
bool removeKnownWiFiNetwork(const String& ssid);
bool moveKnownWiFiNetworkUp(const String& ssid);
bool moveKnownWiFiNetworkDown(const String& ssid);
bool promoteKnownWiFiNetwork(const String& ssid);
bool hasAnyKnownWiFiNetwork();

#endif
