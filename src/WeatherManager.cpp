#include <WiFi.h>
#include <Arduino.h>
#include <time.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include "esp_heap_caps.h"

#include "WeatherManager.h"
#include "SettingsManager.h"
#include "TideService.h"
#include "TimeManager.h"
#include "ui.h"

static volatile bool s_tideCurveDirty = false;
static volatile bool g_weatherUpdated = false;
static volatile bool g_ntpSynced = false;
static time_t g_ntpEpoch = 0;

String api_key = "";
String latitude = "";
String longitude = "";
String units = "metric";
String language = "en";

const char* ntpServer = "pool.ntp.org";
const long utcOffsetInSeconds = 0;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, ntpServer, utcOffsetInSeconds);

static TideService* g_tideService = nullptr;
static TideState g_tideState;

WeatherData currentWeatherData;

static bool fetchCurrentWeatherHTTP(WeatherData& out);

static void log_heap_detailed(const char* tag)
{
    uint32_t freeDefault = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    uint32_t largestDefault = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
    uint32_t freeInternal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    uint32_t largestInternal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    uint32_t freePsram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    uint32_t largestPsram = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);

    Serial.printf("[%s]\n", tag);
    Serial.printf("  DEFAULT heap:   free=%u, largest=%u\n", (unsigned)freeDefault, (unsigned)largestDefault);
    Serial.printf("  INTERNAL heap:  free=%u, largest=%u\n", (unsigned)freeInternal, (unsigned)largestInternal);
    Serial.printf("  PSRAM:          free=%u, largest=%u\n", (unsigned)freePsram, (unsigned)largestPsram);
}

void WeatherManagerBegin()
{
    api_key = currentSettings.weather_api_key;
    latitude = currentSettings.weather_lat;
    longitude = currentSettings.weather_long;

    time_manager_apply_timezone_offset_seconds(currentSettings.timezone_offset_seconds);

    if (g_tideService) {
        delete g_tideService;
        g_tideService = nullptr;
    }

    if (currentSettings.tide_api_key.length() > 0 && latitude.length() > 0 && longitude.length() > 0) {
        g_tideService = new TideService(currentSettings.tide_api_key.c_str(), latitude.toDouble(), longitude.toDouble());
        Serial.println("[Weather] Tide service configured from settings.");
    } else {
        Serial.println("[Weather] Tide service not configured; missing API key or location.");
    }

    WeatherLoadCached();
    TideLoadCached();
}

bool WeatherLoadCached()
{
    if (!LittleFS.exists("/weather.json")) {
        Serial.println("[Weather] No cached weather file available.");
        return false;
    }

    if (!loadWeatherDataFromFile("/weather.json", currentWeatherData)) {
        Serial.println("[Weather] Failed to load cached weather.");
        return false;
    }

    if (currentWeatherData.temperature.length() == 0 || currentWeatherData.id == 0) {
        Serial.println("[Weather] Cached weather exists but is incomplete.");
        return false;
    }

    Serial.printf("[Weather] Loaded cached weather: temp=%s id=%u dt=%lu\n",
                  currentWeatherData.temperature.c_str(),
                  currentWeatherData.id,
                  currentWeatherData.dt);
    return true;
}

bool TideLoadCached()
{
    if (!g_tideService) {
        Serial.println("[Tide] Tide service not configured; cannot load cached tide.");
        return false;
    }

    if (!g_tideService->loadCachedState(g_tideState)) {
        Serial.println("[Tide] No valid cached tide data available.");
        return false;
    }

    Serial.printf("[Tide] Loaded cached tide: %u extremes\n", (unsigned)g_tideState.count);
    WeatherManager_MarkTideCurveDirty();
    return true;
}

bool WeatherUpdate()
{
    g_weatherUpdated = false;

    if (currentWeatherData.temperature.length() == 0) {
        WeatherLoadCached();
    }
    if (g_tideState.count < 2) {
        TideLoadCached();
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[Weather] No WiFi; keeping cached data.");
        return false;
    }

    if (api_key.length() == 0 || latitude.length() == 0 || longitude.length() == 0) {
        Serial.println("[Weather] Missing weather API key or location; keeping cached data.");
        return false;
    }

    Serial.println("[Weather] Initializing NTP...");
    timeClient.begin();

    if (!timeClient.update()) {
        Serial.println("[Weather] Failed to get time from NTP server.");
    } else {
        time_t currentTime = timeClient.getEpochTime();
        struct timeval tv = { .tv_sec = currentTime, .tv_usec = 0 };
        settimeofday(&tv, nullptr);
        g_ntpEpoch = currentTime;
        g_ntpSynced = true;
        Serial.printf("[Weather] Time synchronized with NTP: %s\n", ctime(&currentTime));
    }

    log_heap_detailed("TideService: before HTTPS");

    if (g_tideService) {
        constexpr uint16_t TIDE_HORIZON_HOURS = 48;
        TideUpdateResult tr = g_tideService->update(TIDE_HORIZON_HOURS, g_tideState);

        switch (tr) {
            case TideUpdateResult::Ok:
                Serial.println("[Tide] Tide data updated.");
                WeatherManager_MarkTideCurveDirty();
                break;
            case TideUpdateResult::SkippedRateLimit:
                Serial.println("[Tide] Tide fetch skipped; using cached tide.");
                WeatherManager_MarkTideCurveDirty();
                break;
            case TideUpdateResult::TimeNotReady:
                Serial.println("[Tide] Time not ready yet, keeping cached tide.");
                TideLoadCached();
                break;
            case TideUpdateResult::NetworkError:
            case TideUpdateResult::HttpError:
            case TideUpdateResult::ParseError:
                Serial.printf("[Tide] Tide update failed (%d); keeping cached tide.\n", (int)tr);
                TideLoadCached();
                break;
        }
    }

    updateWeatherData();
    return g_weatherUpdated;
}

void saveWeatherDataToFile(const char* filePath, const WeatherData& weather)
{
    File file = LittleFS.open(filePath, "w");
    if (!file) {
        Serial.println("Failed to open file for writing");
        return;
    }

    DynamicJsonDocument doc(512);
    doc["temperature"] = weather.temperature;
    doc["condition"] = weather.condition;
    doc["icon"] = weather.icon;
    doc["sunrise"] = weather.sunrise;
    doc["sunset"] = weather.sunset;
    doc["wind_speed"] = weather.wind_speed;
    doc["humidity"] = weather.humidity;
    doc["lastUpdate"] = weather.lastUpdate;
    doc["id"] = weather.id;
    doc["moonphase"] = weather.moonphase;
    doc["dt"] = weather.dt;

    if (serializeJson(doc, file) == 0) {
        Serial.println("Failed to write to file");
    }

    file.close();
    Serial.println("Weather data saved successfully");
}

bool loadWeatherDataFromFile(const char* filePath, WeatherData& weather)
{
    File file = LittleFS.open(filePath, FILE_READ);
    if (!file) {
        Serial.println("Failed to open file for reading");
        return false;
    }

    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        Serial.print("Failed to read file: ");
        Serial.println(error.c_str());
        return false;
    }

    weather.temperature = doc["temperature"].as<String>();
    weather.condition = doc["condition"].as<String>();
    weather.icon = doc["icon"].as<String>();
    weather.sunrise = doc["sunrise"].as<String>();
    weather.sunset = doc["sunset"].as<String>();
    weather.wind_speed = doc["wind_speed"].as<String>();
    weather.humidity = doc["humidity"].as<String>();
    weather.moonphase = doc["moonphase"].as<String>();
    weather.lastUpdate = doc["lastUpdate"].as<unsigned long>();
    weather.id = doc["id"].as<uint16_t>();
    weather.dt = doc["dt"].as<unsigned long>();

    Serial.println("Weather data loaded successfully");
    return true;
}

void initializeWeatherData()
{
    const char* filePath = "/weather.json";

    if (!LittleFS.exists(filePath)) {
        Serial.printf("File %s does not exist. Creating a default file.\n", filePath);

        WeatherData defaultWeather;
        defaultWeather.temperature = "N/A";
        defaultWeather.condition = "Unknown";
        defaultWeather.icon = "unknown";
        defaultWeather.sunrise = "N/A";
        defaultWeather.sunset = "N/A";
        defaultWeather.wind_speed = "0";
        defaultWeather.humidity = "0";
        defaultWeather.moonphase = "0";
        defaultWeather.lastUpdate = 0;
        defaultWeather.id = 666;
        defaultWeather.dt = 0;
        saveWeatherDataToFile(filePath, defaultWeather);
    }

    WeatherLoadCached();
}

void updateWeatherData()
{
    if (currentWeatherData.temperature.length() == 0) {
        WeatherLoadCached();
    }

    Serial.printf("Weather data timestamp (UNIX): %lu\n", currentWeatherData.dt);
    Serial.println("Fetching new weather data...");
    Serial.printf("Free heap: %u, largest block: %u\n", esp_get_free_heap_size(), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

    WeatherData freshWeather = currentWeatherData;
    if (!fetchCurrentWeatherHTTP(freshWeather)) {
        Serial.println("[Weather] fetchCurrentWeatherHTTP failed; keeping cached weather data.");
        return;
    }

    currentWeatherData = freshWeather;
    saveWeatherDataToFile("/weather.json", currentWeatherData);

    Serial.println("Setting current Weather Data to display:");
    if (ui_WeatherLabel) {
        lv_label_set_text(ui_WeatherLabel, currentWeatherData.temperature.c_str());
    }

    const char* iconPath = getMeteoconIcon(currentWeatherData.id, true);
    Serial.printf("Icon path: %s\n", iconPath);
    if (ui_WeatherImage) {
        lv_img_set_src(ui_WeatherImage, iconPath);
    }

    Serial.printf("[UI] Setting temp label from currentWeatherData.temperature='%s' id=%u\n",
                  currentWeatherData.temperature.c_str(), currentWeatherData.id);
}

String strTime(time_t unixTime)
{
    return ctime(&unixTime);
}

const char* getMeteoconIcon(uint16_t id, bool today)
{
    if (today && id / 100 == 8 && (currentWeatherData.sunrise < currentWeatherData.sunset)) id += 1000;
    if (id == 666) return "A:/lvgl/icons/unknown.png";
    if (id / 100 == 2) return "A:/lvgl/icons/thunderstorm.png";
    if (id / 100 == 3) return "A:/lvgl/icons/drizzle.png";
    if (id / 100 == 4) return "A:/lvgl/icons/unknown.png";
    if (id == 500) return "A:/lvgl/icons/light-rain.png";
    else if (id == 511) return "A:/lvgl/icons/sleet.png";
    else if (id / 100 == 5) return "A:/lvgl/icons/rain.png";
    if (id >= 611 && id <= 616) return "A:/lvgl/icons/sleet.png";
    else if (id / 100 == 6) return "A:/lvgl/icons/snow.png";
    if (id / 100 == 7) return "A:/lvgl/icons/fog.png";
    if (id == 800) return "A:/lvgl/icons/clear-day.png";
    if (id == 801) return "A:/lvgl/icons/partly-cloudy-day.png";
    if (id == 802 || id == 803 || id == 804) return "A:/lvgl/icons/cloudy.png";
    if (id == 1800) return "A:/lvgl/icons/clear-night.png";
    if (id == 1801) return "A:/lvgl/icons/partly-cloudy-night.png";
    if (id == 1802 || id == 1803 || id == 1804) return "A:/lvgl/icons/cloudy.png";
    return "A:/lvgl/icons/unknown.png";
}

void printCurrentWeather()
{
    fetchCurrentWeatherHTTP(currentWeatherData);
}

static bool fetchCurrentWeatherHTTP(WeatherData& out)
{
    if (WiFi.status() != WL_CONNECTED) return false;

    WiFiClient client;
    HTTPClient http;

    String url = "http://api.openweathermap.org/data/2.5/weather?lat=" + latitude +
                 "&lon=" + longitude +
                 "&units=" + units +
                 "&lang=" + language +
                 "&appid=" + api_key;

    if (!http.begin(client, url)) {
        Serial.println("[Weather] http.begin failed");
        return false;
    }

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("[Weather] HTTP GET failed: %d\n", code);
        http.end();
        return false;
    }

    StaticJsonDocument<2048> doc;
    DeserializationError err = deserializeJson(doc, http.getStream());
    http.end();

    if (err) {
        Serial.printf("[Weather] JSON parse failed: %s\n", err.c_str());
        return false;
    }

    int32_t tzOffset = doc["timezone"] | currentSettings.timezone_offset_seconds;
    if (tzOffset != currentSettings.timezone_offset_seconds) {
        currentSettings.timezone_offset_seconds = tzOffset;
        saveSettingsDataToFile("/settings.json", currentSettings);
    }
    time_manager_apply_timezone_offset_seconds(tzOffset);

    float temp = doc["main"]["temp"] | NAN;
    uint16_t id = doc["weather"][0]["id"] | 666;
    const char* desc = doc["weather"][0]["description"] | "Unknown";
    const char* icon = doc["weather"][0]["icon"] | "";

    out.temperature = String(temp, 1) + "°C";
    out.condition = desc;
    out.icon = icon;
    out.id = id;
    out.dt = doc["dt"] | (unsigned long)time(nullptr);
    out.lastUpdate = (unsigned long)time(nullptr);
    out.sunrise = strTime((time_t)(doc["sys"]["sunrise"] | 0));
    out.sunset = strTime((time_t)(doc["sys"]["sunset"] | 0));
    out.humidity = String((int)(doc["main"]["humidity"] | 0)) + "%";
    out.wind_speed = String((float)(doc["wind"]["speed"] | 0.0f), 1) + " m/s";

    Serial.println("[Weather] Fetched current weather via HTTP.");
    g_weatherUpdated = true;
    return true;
}

const WeatherData& WeatherGet()
{
    return currentWeatherData;
}

const TideState& TideGet()
{
    return g_tideState;
}

bool WeatherManager_GetTideCurve(float* heights, uint16_t maxSamples, uint16_t& outCount, time_t& outFirstSampleUtc, uint32_t& outStepSeconds)
{
    if (!heights || maxSamples < 2) {
        Serial.println("[WeatherManager] GetTideCurve: invalid buffer");
        return false;
    }

    if (g_tideState.count < 2) {
        Serial.printf("[WeatherManager] GetTideCurve: not enough extremes (%u)\n", (unsigned)g_tideState.count);
        return false;
    }

    bool ok = TideBuildSampleCurve(g_tideState, heights, maxSamples, outCount, outFirstSampleUtc, outStepSeconds);
    if (!ok) {
        Serial.println("[WeatherManager] GetTideCurve: TideBuildSampleCurve failed");
        return false;
    }

    Serial.printf("[WeatherManager] GetTideCurve: count=%u, step=%u s\n", (unsigned)outCount, (unsigned)outStepSeconds);
    return true;
}

void WeatherManager_MarkTideCurveDirty()
{
    s_tideCurveDirty = true;
}

bool WeatherManager_TakeTideCurveDirtyFlag()
{
    if (!s_tideCurveDirty) return false;
    s_tideCurveDirty = false;
    return true;
}

bool WeatherConsumeNtpSync(time_t *outEpoch)
{
    if (!g_ntpSynced) return false;
    if (outEpoch) *outEpoch = g_ntpEpoch;
    g_ntpSynced = false;
    return true;
}

void WeatherInit()
{
    WeatherManagerBegin();
}
