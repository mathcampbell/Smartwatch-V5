#ifndef SETTINGS_MANAGER_H

#define SETTINGS_MANAGER_H
// IMPORTS //
#include <WiFi.h>
#include <JSON_Decoder.h>
#include <Arduino.h>
#include <Time.h>
#include "WeatherManager.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include "ui.h"
#include <vector>

struct WiFiNetwork {
    String ssid;
    String password;
};

struct SettingsData {
    String wifi_ssd;
    String wifi_pass;
    unsigned long lastUpdate; 
    uint16_t brightness_level;
    uint16_t screen_dim_duration;
    uint16_t sleep_duration;
    uint16_t system_volume;
    String weather_lat;
    String weather_long;
    std::vector<WiFiNetwork> known_wifi_networks;

};



extern SettingsData currentSettings;

bool loadSettingsDataFromFile(const char* filePath, SettingsData& settings);
void saveSettingsDataToFile(const char* filePath, const SettingsData& settings);
void initializeSettingsData();


#endif