#include "SettingsManager.h"

#include <algorithm>

const char* filePath = "/settings.json";
SettingsData currentSettings;

static void seedLegacyWifiIntoKnownNetworks(SettingsData& settings)
{
    if (settings.wifi_ssid.length() == 0) return;

    for (const auto& network : settings.known_wifi_networks) {
        if (network.ssid == settings.wifi_ssid) return;
    }

    WiFiNetwork legacy;
    legacy.ssid = settings.wifi_ssid;
    legacy.password = settings.wifi_pass;
    settings.known_wifi_networks.push_back(legacy);
}

bool loadSettingsDataFromFile(const char* filePath, SettingsData& settings)
{
    File file = LittleFS.open(filePath, FILE_READ);
    if (!file) {
        Serial.println("Failed to open file for reading");
        return false;
    }

    DynamicJsonDocument doc(4096);

    DeserializationError error = deserializeJson(doc, file);
    if (error) {
        Serial.print("Failed to read file: ");
        Serial.println(error.c_str());
        file.close();
        return false;
    }

    settings.wifi_ssid = doc["wifi_ssid"].isNull() ? doc["wifi_ssd"].as<String>() : doc["wifi_ssid"].as<String>();
    settings.wifi_ssd = settings.wifi_ssid;
    settings.wifi_pass = doc["wifi_pass"].as<String>();
    settings.weather_api_key = doc["weather_api_key"].as<String>();
    settings.tide_api_key = doc["tide_api_key"].as<String>();
    settings.location_name = doc["location_name"].as<String>();
    settings.weather_lat = doc["weather_lat"].as<String>();
    settings.weather_long = doc["weather_long"].as<String>();
    settings.timezone_offset_seconds = doc["timezone_offset_seconds"] | 0;
    settings.lastUpdate = doc["lastUpdate"].as<unsigned long>();
    settings.brightness_level = doc["brightness_level"] | 70;
    settings.screen_dim_duration = doc["screen_dim_duration"] | 20;
    settings.sleep_duration = doc["sleep_duration"] | 30;
    settings.system_volume = doc["system_volume"] | 50;

    settings.known_wifi_networks.clear();
    JsonArray wifiNetworks = doc["known_wifi_networks"].as<JsonArray>();
    for (JsonObject network : wifiNetworks) {
        WiFiNetwork wifiNetwork;
        wifiNetwork.ssid = network["ssid"].as<String>();
        wifiNetwork.password = network["password"].as<String>();
        if (wifiNetwork.ssid.length() > 0) {
            settings.known_wifi_networks.push_back(wifiNetwork);
        }
    }

    seedLegacyWifiIntoKnownNetworks(settings);

    file.close();
    Serial.println("Setting data loaded successfully");
    return true;
}

void saveSettingsDataToFile(const char* filePath, const SettingsData& settings)
{
    File file = LittleFS.open(filePath, "w");
    if (!file) {
        Serial.println("Failed to open file for writing");
        return;
    }

    DynamicJsonDocument doc(4096);

    doc["wifi_ssid"] = settings.wifi_ssid;
    doc["wifi_pass"] = settings.wifi_pass;
    doc["weather_api_key"] = settings.weather_api_key;
    doc["tide_api_key"] = settings.tide_api_key;
    doc["location_name"] = settings.location_name;
    doc["weather_lat"] = settings.weather_lat;
    doc["weather_long"] = settings.weather_long;
    doc["timezone_offset_seconds"] = settings.timezone_offset_seconds;
    doc["lastUpdate"] = settings.lastUpdate;
    doc["brightness_level"] = settings.brightness_level;
    doc["screen_dim_duration"] =  settings.screen_dim_duration;
    doc["sleep_duration"] =  settings.sleep_duration;
    doc["system_volume"] = settings.system_volume;

    JsonArray wifiNetworks = doc.createNestedArray("known_wifi_networks");
    for (const auto& network : settings.known_wifi_networks) {
        if (network.ssid.length() == 0) continue;
        JsonObject networkObj = wifiNetworks.createNestedObject();
        networkObj["ssid"] = network.ssid;
        networkObj["password"] = network.password;
    }

    if (serializeJsonPretty(doc, file) == 0) {
        Serial.println("Failed to write to file");
    }

    file.close();
    Serial.println("Setting data saved successfully");
}

void initializeSettingsData()
{
    if (!LittleFS.exists(filePath)) {
        Serial.printf("File %s does not exist. Creating a safe default file.\n", filePath);

        SettingsData defaultSettings;
        defaultSettings.wifi_ssid = "";
        defaultSettings.wifi_ssd = "";
        defaultSettings.wifi_pass = "";
        defaultSettings.weather_api_key = "";
        defaultSettings.tide_api_key = "";
        defaultSettings.location_name = "";
        defaultSettings.timezone_offset_seconds = 0;
        defaultSettings.lastUpdate = 0;
        defaultSettings.brightness_level = 70;
        defaultSettings.screen_dim_duration = 20;
        defaultSettings.sleep_duration = 30;
        defaultSettings.system_volume = 50;
        defaultSettings.weather_lat = "";
        defaultSettings.weather_long = "";

        saveSettingsDataToFile(filePath, defaultSettings);
        currentSettings = defaultSettings;
    }
    else
    {
        loadSettingsDataFromFile("/settings.json", currentSettings);
    }
}

int findKnownWiFiNetworkIndex(const String& ssid)
{
    for (size_t i = 0; i < currentSettings.known_wifi_networks.size(); ++i) {
        if (currentSettings.known_wifi_networks[i].ssid == ssid) return (int)i;
    }
    return -1;
}

bool addOrUpdateKnownWiFiNetwork(const String& ssid, const String& password, bool makeFirst)
{
    String cleanSsid = ssid;
    cleanSsid.trim();
    if (cleanSsid.length() == 0) return false;

    int idx = findKnownWiFiNetworkIndex(cleanSsid);
    if (idx >= 0) {
        currentSettings.known_wifi_networks[idx].password = password;
        if (makeFirst && idx > 0) {
            WiFiNetwork network = currentSettings.known_wifi_networks[idx];
            currentSettings.known_wifi_networks.erase(currentSettings.known_wifi_networks.begin() + idx);
            currentSettings.known_wifi_networks.insert(currentSettings.known_wifi_networks.begin(), network);
        }
    } else {
        WiFiNetwork network;
        network.ssid = cleanSsid;
        network.password = password;
        if (makeFirst) currentSettings.known_wifi_networks.insert(currentSettings.known_wifi_networks.begin(), network);
        else currentSettings.known_wifi_networks.push_back(network);
    }

    currentSettings.wifi_ssid = currentSettings.known_wifi_networks.front().ssid;
    currentSettings.wifi_ssd = currentSettings.wifi_ssid;
    currentSettings.wifi_pass = currentSettings.known_wifi_networks.front().password;
    saveSettingsDataToFile("/settings.json", currentSettings);
    return true;
}

bool removeKnownWiFiNetwork(const String& ssid)
{
    int idx = findKnownWiFiNetworkIndex(ssid);
    if (idx < 0) return false;

    currentSettings.known_wifi_networks.erase(currentSettings.known_wifi_networks.begin() + idx);
    if (!currentSettings.known_wifi_networks.empty()) {
        currentSettings.wifi_ssid = currentSettings.known_wifi_networks.front().ssid;
        currentSettings.wifi_ssd = currentSettings.wifi_ssid;
        currentSettings.wifi_pass = currentSettings.known_wifi_networks.front().password;
    } else {
        currentSettings.wifi_ssid = "";
        currentSettings.wifi_ssd = "";
        currentSettings.wifi_pass = "";
    }

    saveSettingsDataToFile("/settings.json", currentSettings);
    return true;
}

bool moveKnownWiFiNetworkUp(const String& ssid)
{
    int idx = findKnownWiFiNetworkIndex(ssid);
    if (idx <= 0) return false;

    std::swap(currentSettings.known_wifi_networks[idx], currentSettings.known_wifi_networks[idx - 1]);
    currentSettings.wifi_ssid = currentSettings.known_wifi_networks.front().ssid;
    currentSettings.wifi_ssd = currentSettings.wifi_ssid;
    currentSettings.wifi_pass = currentSettings.known_wifi_networks.front().password;
    saveSettingsDataToFile("/settings.json", currentSettings);
    return true;
}

bool moveKnownWiFiNetworkDown(const String& ssid)
{
    int idx = findKnownWiFiNetworkIndex(ssid);
    if (idx < 0 || idx >= (int)currentSettings.known_wifi_networks.size() - 1) return false;

    std::swap(currentSettings.known_wifi_networks[idx], currentSettings.known_wifi_networks[idx + 1]);
    currentSettings.wifi_ssid = currentSettings.known_wifi_networks.front().ssid;
    currentSettings.wifi_ssd = currentSettings.wifi_ssid;
    currentSettings.wifi_pass = currentSettings.known_wifi_networks.front().password;
    saveSettingsDataToFile("/settings.json", currentSettings);
    return true;
}

bool promoteKnownWiFiNetwork(const String& ssid)
{
    int idx = findKnownWiFiNetworkIndex(ssid);
    if (idx < 0) return false;
    if (idx == 0) return true;

    WiFiNetwork network = currentSettings.known_wifi_networks[idx];
    currentSettings.known_wifi_networks.erase(currentSettings.known_wifi_networks.begin() + idx);
    currentSettings.known_wifi_networks.insert(currentSettings.known_wifi_networks.begin(), network);
    currentSettings.wifi_ssid = network.ssid;
    currentSettings.wifi_ssd = network.ssid;
    currentSettings.wifi_pass = network.password;
    saveSettingsDataToFile("/settings.json", currentSettings);
    return true;
}

bool hasAnyKnownWiFiNetwork()
{
    return !currentSettings.known_wifi_networks.empty() || currentSettings.wifi_ssid.length() > 0;
}
