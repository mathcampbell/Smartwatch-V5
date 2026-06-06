#include "SettingsManager.h"

const char* filePath = "/settings.json";
SettingsData currentSettings;

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
        settings.known_wifi_networks.push_back(wifiNetwork);
    }

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
    doc["lastUpdate"] = settings.lastUpdate;
    doc["brightness_level"] = settings.brightness_level;
    doc["screen_dim_duration"] =  settings.screen_dim_duration;
    doc["sleep_duration"] =  settings.sleep_duration;
    doc["system_volume"] = settings.system_volume;

    JsonArray wifiNetworks = doc.createNestedArray("known_wifi_networks");
    for (const auto& network : settings.known_wifi_networks) {
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
