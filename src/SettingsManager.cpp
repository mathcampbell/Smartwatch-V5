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

    DynamicJsonDocument doc(1024); // Adjust size as needed

    DeserializationError error = deserializeJson(doc, file);
    if (error) {
        Serial.print("Failed to read file: ");
        Serial.println(error.c_str());
        file.close();
        return false;
    }

    settings.wifi_ssd = doc["wifi_ssd"].as<String>();
    settings.wifi_pass = doc["wifi_pass"].as<String>();
    settings.weather_lat = doc["weather_lat"].as<String>();
    settings.weather_long = doc["weather_lat"].as<String>();
    settings.lastUpdate = doc["lastUpdate"].as<unsigned long>();
    settings.brightness_level = doc["brightness_level"].as<uint16_t>(); 
    settings.screen_dim_duration = doc["screen_dim_duration"].as<uint16_t>(); 
    settings.sleep_duration = doc["sleep_duration"].as<uint16_t>(); 
    settings.system_volume = doc["system_volume"].as<uint16_t>(); 

      // Load known Wi-Fi networks
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

    DynamicJsonDocument doc(512); // Adjust size as needed

    doc["wifi_ssd"] = settings.wifi_ssd;
    doc["wifi_pass"] = settings.wifi_pass;
    doc["weather_lat"] = settings.weather_lat;
    doc["weather_long"] = settings.weather_long;
    doc["lastUpdate"] = settings.lastUpdate;
    doc["brightness_level"] = settings.brightness_level;
    doc["screen_dim_duration"] =  settings.screen_dim_duration;
    doc["sleep_duration"] =  settings.sleep_duration;
    doc["system_volume"] = settings.system_volume;
     // Save known Wi-Fi networks
    JsonArray wifiNetworks = doc.createNestedArray("known_wifi_networks");
    for (const auto& network : settings.known_wifi_networks) {
        JsonObject networkObj = wifiNetworks.createNestedObject();
        networkObj["ssid"] = network.ssid;
        networkObj["password"] = network.password;
    }

    if (serializeJson(doc, file) == 0) {
        Serial.println("Failed to write to file");
    }

    file.close();
    Serial.println("Weather data saved successfully");
}


void initializeSettingsData()
{
    if (!LittleFS.exists(filePath)) {
        Serial.printf("File %s does not exist. Creating a default file.\n", filePath);
          SettingsData defaultSettings;
            defaultSettings.wifi_ssd = "GraphicsForge_A";
            defaultSettings.wifi_pass = "25137916";
            defaultSettings.lastUpdate = 0;
            defaultSettings.brightness_level = 70;
            defaultSettings.screen_dim_duration = 20;
            defaultSettings.sleep_duration = 30;
            defaultSettings.system_volume = 50;
            defaultSettings.weather_lat = "56.0089507";
            defaultSettings.weather_long = "-4.7990904";


        // Initialize known Wi-Fi networks list
        WiFiNetwork initialNetwork;
        initialNetwork.ssid = "GraphicsForge_A";
        initialNetwork.password = "25137916";
        defaultSettings.known_wifi_networks.push_back(initialNetwork);
        
            saveSettingsDataToFile(filePath, defaultSettings);
        }
    else 
    {
         loadSettingsDataFromFile("/settings.json", currentSettings);
    
    }
}
