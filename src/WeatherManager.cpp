// IMPORTS //
#include <WiFi.h>
#include <JSON_Decoder.h>
#include <OpenWeather.h>
#include <Arduino.h>
#include <Time.h>
#include "WeatherManager.h"
#include "ui.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <HTTPClient.h>
#include "esp_heap_caps.h"
// Tide imports
#include "TideService.h"

// DEFINES //

#define TIME_OFFSET 1UL * 3600UL // UTC + 0 hour



// VARIABLES //


static volatile bool s_tideCurveDirty = false;
static volatile bool g_weatherUpdated = false;
static volatile bool g_ntpSynced = false;
static time_t g_ntpEpoch = 0;

// OpenWeather API Details, replace x's with your API key
String api_key = "3a31e88719b05f19b116b6acb55e883c"; // Obtain this from your OpenWeather account

// Set both your longitude and latitude to at least 4 decimal places
String latitude =  "56.0089507"; // 90.0000 to -90.0000 negative for Southern hemisphere
String longitude = "-4.7990904"; // 180.000 to -180.000 negative for West

String units = "metric";  // or "imperial"
String language = "en";   // See notes tab

// NTP settings
const char* ntpServer = "pool.ntp.org";  // Use a public NTP server
const long utcOffsetInSeconds = 0;      // Adjust as necessary for your timezone
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, ntpServer, utcOffsetInSeconds);

// Stormglass API key – put your real key here or pull from settings later
static const char* STORMGLASS_API_KEY = "c6666db4-fe38-11f0-b30d-0242ac120004-c6666e36-fe38-11f0-b30d-0242ac120004";


// Tide service + state – reuse same lat/long as weather
static TideService g_tideService(STORMGLASS_API_KEY,
                                 latitude.toDouble(),
                                 longitude.toDouble());
static TideState g_tideState;


// Main part starts here //
OW_Weather ow; // Weather forecast library instance

WeatherData currentWeatherData; // Global or instance variable

static void log_heap_detailed(const char* tag)
{
    uint32_t freeDefault      = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    uint32_t largestDefault   = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);

    uint32_t freeInternal     = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    uint32_t largestInternal  = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    uint32_t freePsram        = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    uint32_t largestPsram     = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);

    Serial.printf("[%s]\n", tag);
    Serial.printf("  DEFAULT heap:   free=%u, largest=%u\n",
                  (unsigned)freeDefault, (unsigned)largestDefault);
    Serial.printf("  INTERNAL heap:  free=%u, largest=%u\n",
                  (unsigned)freeInternal, (unsigned)largestInternal);
    Serial.printf("  PSRAM:          free=%u, largest=%u\n",
                  (unsigned)freePsram, (unsigned)largestPsram);
}


bool WeatherUpdate()
{
    // REQUIREMENT: caller ensured WiFi is connected.
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[Weather] No WiFi; skipping update.");
        return false;
    }

    // ---- NTP sync (keep your existing NTPClient object) ----
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


    // ---- tide logic ----
    constexpr uint16_t TIDE_HORIZON_HOURS = 48;
    TideUpdateResult tr = g_tideService.update(TIDE_HORIZON_HOURS, g_tideState);

    switch (tr) {
        case TideUpdateResult::Ok:
            Serial.println("[Tide] Tide data updated.");
             WeatherManager_MarkTideCurveDirty();   // ✅ tell the UI "new curve ready"
            break;
        case TideUpdateResult::SkippedRateLimit:
            // Normal; we’re still inside the 3h cooldown
            break;
        case TideUpdateResult::TimeNotReady:
            Serial.println("[Tide] Time not ready yet, skipping tide update.");
            break;
        case TideUpdateResult::NetworkError:
        case TideUpdateResult::HttpError:
        case TideUpdateResult::ParseError:
            Serial.printf("[Tide] Tide update failed (%d)\n", (int)tr);
            break;
    }
    // ---- tide logic ends ----


    // ---- your existing weather logic ----
    initializeWeatherData();
    // If your initializeWeatherData() doesn’t actually fetch new weather, and you have
    // a separate fetch function, call it here instead.
    return true;
}

void saveWeatherDataToFile(const char* filePath, const WeatherData& weather) {
    File file = LittleFS.open(filePath, "w");
    if (!file) {
        Serial.println("Failed to open file for writing");
        return;
    }

    DynamicJsonDocument doc(512); // Adjust size as needed

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

bool loadWeatherDataFromFile(const char* filePath, WeatherData& weather) {
    File file = LittleFS.open(filePath, FILE_READ);
    if (!file) {
        Serial.println("Failed to open file for reading");
        return false;
    }

    DynamicJsonDocument doc(512); // Adjust size as needed

    DeserializationError error = deserializeJson(doc, file);
    if (error) {
        Serial.print("Failed to read file: ");
        Serial.println(error.c_str());
        file.close();
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

    file.close();
    Serial.println("Weather data loaded successfully");
    return true;
}

void initializeWeatherData() {
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

                // Use system time if available, fallback to 0 if not
        time_t currentTime = time(nullptr);
        if (currentTime == -1) {
            Serial.println("Failed to get system time. Defaulting to epoch.");
            defaultWeather.dt = 0;  // Fallback to epoch
        } else {
            defaultWeather.dt = static_cast<unsigned long>(currentTime);
            Serial.printf("Default weather timestamp set to: %lu\n", defaultWeather.dt);
        }

        saveWeatherDataToFile(filePath, defaultWeather);
       // printCurrentWeather();
    }
    updateWeatherData();
}


void updateWeatherData() {
    // Get the current time from the system
    time_t currentTime = time(nullptr);

    if (currentTime == -1) {
        Serial.println("Failed to get system time. Skipping weather update.");
        return;  // Exit if the system time is not available
    }

    // Load weather data from file
    if (!loadWeatherDataFromFile("/weather.json", currentWeatherData)) {
        Serial.println("Failed to load weather data. Initializing defaults.");
        currentWeatherData.dt = 0;  // Force fetch on first run
    }

    // Print current and last update times for debugging
    Serial.printf("Current time (UNIX): %ld\n", currentTime);
    Serial.printf("Weather data timestamp (UNIX): %ld\n", currentWeatherData.dt);
    Serial.printf("Time since last update (seconds): %ld\n", currentTime - currentWeatherData.dt);

    // Check if the weather data needs to be updated
    if (currentWeatherData.dt == 0 || (currentTime - currentWeatherData.dt) >= 360) {
        Serial.println("Fetching new weather data...");

        // Fetch new weather data
        //printCurrentWeather();

        // Update `dt` with the current system time
        //currentWeatherData.dt = static_cast<unsigned long>(currentTime);

        // Save updated weather data to file
        Serial.printf("Free heap: %u, largest block: %u\n",
              esp_get_free_heap_size(),
              heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        if (!fetchCurrentWeatherHTTP(currentWeatherData)) {
    Serial.println("[Weather] fetchCurrentWeatherHTTP failed");
    return;
}

        saveWeatherDataToFile("/weather.json", currentWeatherData); 
    }

    else {
        Serial.println("Weather data is up-to-date. Skipping fetch.");
         if (!loadWeatherDataFromFile("/weather.json", currentWeatherData)) {
        Serial.println("Failed to load weather data from file. Initializing defaults.");
        currentWeatherData.dt = 0;  // Force fetch on first run
         //printCurrentWeather();

        // Update `dt` with the current system time
        //currentWeatherData.dt = static_cast<unsigned long>(currentTime);

        Serial.printf("Free heap: %u, largest block: %u\n",
              esp_get_free_heap_size(),
              heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        if (!fetchCurrentWeatherHTTP(currentWeatherData)) {
    Serial.println("[Weather] fetchCurrentWeatherHTTP failed");
    return;
}

        // Save updated weather data to file
        saveWeatherDataToFile("/weather.json", currentWeatherData); 
    }
  }
    
    Serial.println("Setting current Weather Data to display:");

    // Update the UI with current weather data
    lv_label_set_text(ui_WeatherLabel, currentWeatherData.temperature.c_str());

    // Get the file path for the icon
    const char* iconPath = getMeteoconIcon(currentWeatherData.id, true);
    Serial.printf("Icon path: %s\n", iconPath);

    // Update the weather icon on the UI
    lv_img_set_src(ui_WeatherImage, iconPath);
  //  lv_color_t sci_fi_blue = lv_color_make(0, 200, 255); // Cyan blue color
   // lv_obj_set_style_img_recolor(ui_WeatherImage, sci_fi_blue, LV_PART_MAIN);
   // lv_obj_set_style_img_recolor_opa(ui_WeatherImage, LV_OPA_90, LV_PART_MAIN);

    Serial.println("Weather UI updated. Now doing a sanity check on the data currently held");

    Serial.printf("[UI] Setting temp label from currentWeatherData.temperature='%s' id=%u\n",
              currentWeatherData.temperature.c_str(), currentWeatherData.id);

   
    
}

/***************************************************************************************
**                          Convert unix time to a time string
***************************************************************************************/
String strTime(time_t unixTime)
{
  unixTime += TIME_OFFSET;
  return ctime(&unixTime);
}




const char* getMeteoconIcon(uint16_t id, bool today)
{
    if (today && id / 100 == 8 && (currentWeatherData.sunrise < currentWeatherData.sunset)) id += 1000; 
    if (id == 666) {
    return "A:/lvgl/icons/unknown.png";
}
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
    if (id == 802) return "A:/lvgl/icons/cloudy.png";
    if (id == 803) return "A:/lvgl/icons/cloudy.png";
    if (id == 804) return "A:/lvgl/icons/cloudy.png";
    if (id == 1800) return "A:/lvgl/icons/clear-night.png";
    if (id == 1801) return "A:/lvgl/icons/partly-cloudy-night.png";
    if (id == 1802) return "A:/lvgl/icons/cloudy.png";
    if (id == 1803) return "A:/lvgl/icons/cloudy.png";
    if (id == 1804) return "A:/lvgl/icons/cloudy.png";
    return "A:/lvgl/icons/unknown.png";
}



void printCurrentWeather()
{
  // Create the structures that hold the retrieved weather
  OW_current *current = new OW_current;
  OW_hourly *hourly = new OW_hourly;
  OW_daily  *daily = new OW_daily;

  Serial.println("\nRequesting weather information from OpenWeather... ");

  //On the ESP8266 (only) the library by default uses BearSSL, another option is to use AXTLS
  //For problems with ESP8266 stability, use AXTLS by adding a false parameter thus       vvvvv
  //ow.getForecast(current, hourly, daily, api_key, latitude, longitude, units, language, false);

  Serial.printf("Free heap: %u, largest block: %u\n",
              esp_get_free_heap_size(),
              heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

  ow.getForecast(current, hourly, daily, api_key, latitude, longitude, units, language);
  Serial.println("");
  Serial.println("Weather from Open Weather\n");

  // Position as reported by Open Weather
  Serial.print("Latitude            : "); Serial.println(ow.lat);
  Serial.print("Longitude           : "); Serial.println(ow.lon);
  // We can use the timezone to set the offset eventually...
  Serial.print("Timezone            : "); Serial.println(ow.timezone);
  Serial.println();

  if (current)
  {
    Serial.println("############### Current weather ###############\n");
    Serial.print("dt (time)        : "); Serial.println(strTime(current->dt));
    Serial.print("sunrise          : "); Serial.println(strTime(current->sunrise));
    Serial.print("sunset           : "); Serial.println(strTime(current->sunset));
    Serial.print("temp             : "); Serial.println(current->temp);
    Serial.print("feels_like       : "); Serial.println(current->feels_like);
    Serial.print("pressure         : "); Serial.println(current->pressure);
    Serial.print("humidity         : "); Serial.println(current->humidity);
    Serial.print("dew_point        : "); Serial.println(current->dew_point);
    Serial.print("uvi              : "); Serial.println(current->uvi);
    Serial.print("clouds           : "); Serial.println(current->clouds);
    Serial.print("visibility       : "); Serial.println(current->visibility);
    Serial.print("wind_speed       : "); Serial.println(current->wind_speed);
    Serial.print("wind_gust        : "); Serial.println(current->wind_gust);
    Serial.print("wind_deg         : "); Serial.println(current->wind_deg);
    Serial.print("rain             : "); Serial.println(current->rain);
    Serial.print("snow             : "); Serial.println(current->snow);
    Serial.println();
    Serial.print("id               : "); Serial.println(current->id);
    Serial.print("main             : "); Serial.println(current->main);
    Serial.print("description      : "); Serial.println(current->description);
    Serial.print("icon             : "); Serial.println(current->icon);

    Serial.println();

    currentWeatherData.temperature = String(current->temp, 1) + "°C";
        currentWeatherData.condition = current->description;
        currentWeatherData.icon = current->icon;
        currentWeatherData.sunrise = strTime(current->sunrise);
        currentWeatherData.sunset = strTime(current->sunset);
        currentWeatherData.wind_speed = String(current->wind_speed, 1) + " m/s";
        currentWeatherData.humidity = String(current->humidity) + "%";
        currentWeatherData.id = uint16_t (current->id);
        currentWeatherData.dt = (current->dt);


    // Setting the UI now.

/*             // Update the WeatherData structure
        currentWeatherData.temperature = String(current->temp, 1) + "°C";
        currentWeatherData.condition = current->description;
        currentWeatherData.icon = current->icon;
        currentWeatherData.sunrise = strTime(current->sunrise);
        currentWeatherData.sunset = strTime(current->sunset);
        currentWeatherData.wind_speed = String(current->wind_speed, 1) + " m/s";
        currentWeatherData.humidity = String(current->humidity) + "%";

      lv_label_set_text(ui_WeatherLabel, currentWeatherData.temperature.c_str());
      //lv_label_set_text(ui_ConditionLabel, currentWeatherData.condition.c_str());

        // Get the file path for the icon
        const char* iconPath = getMeteoconIcon(current->id, true);
        Serial.println("Icon path set:");
        Serial.println(iconPath);

        // Update the image source dynamically
        lv_img_set_src(ui_WeatherImage, iconPath);
         lv_color_t sci_fi_blue = lv_color_make(0, 200, 255); // Cyan blue color
        lv_obj_set_style_img_recolor(ui_WeatherImage, sci_fi_blue, LV_PART_MAIN);
        lv_obj_set_style_img_recolor_opa(ui_WeatherImage, LV_OPA_90, LV_PART_MAIN); */

  }

  if (hourly)
  {
    Serial.println("############### Hourly weather  ###############\n");
    for (int i = 0; i < MAX_HOURS; i++)
    {
      Serial.print("Hourly summary  "); if (i < 10) Serial.print(" "); Serial.print(i);
      Serial.println();
      Serial.print("dt (time)        : "); Serial.println(strTime(hourly->dt[i]));
      Serial.print("temp             : "); Serial.println(hourly->temp[i]);
      Serial.print("feels_like       : "); Serial.println(hourly->feels_like[i]);
      Serial.print("pressure         : "); Serial.println(hourly->pressure[i]);
      Serial.print("humidity         : "); Serial.println(hourly->humidity[i]);
      Serial.print("dew_point        : "); Serial.println(hourly->dew_point[i]);
      Serial.print("clouds           : "); Serial.println(hourly->clouds[i]);
      Serial.print("wind_speed       : "); Serial.println(hourly->wind_speed[i]);
      Serial.print("wind_gust        : "); Serial.println(hourly->wind_gust[i]);
      Serial.print("wind_deg         : "); Serial.println(hourly->wind_deg[i]);
      Serial.print("rain             : "); Serial.println(hourly->rain[i]);
      Serial.print("snow             : "); Serial.println(hourly->snow[i]);
      Serial.println();
      Serial.print("id               : "); Serial.println(hourly->id[i]);
      Serial.print("main             : "); Serial.println(hourly->main[i]);
      Serial.print("description      : "); Serial.println(hourly->description[i]);
      Serial.print("icon             : "); Serial.println(hourly->icon[i]);
      Serial.print("pop              : "); Serial.println(hourly->pop[i]);

      Serial.println();
    }
  }

  if (daily)
  {
    Serial.println("###############  Daily weather  ###############\n");
    for (int i = 0; i < MAX_DAYS; i++)
    {
      Serial.print("Daily summary   "); if (i < 10) Serial.print(" "); Serial.print(i);
      Serial.println();
      Serial.print("dt (time)        : "); Serial.println(strTime(daily->dt[i]));
      Serial.print("sunrise          : "); Serial.println(strTime(daily->sunrise[i]));
      Serial.print("sunset           : "); Serial.println(strTime(daily->sunset[i]));

      Serial.print("temp.morn        : "); Serial.println(daily->temp_morn[i]);
      Serial.print("temp.day         : "); Serial.println(daily->temp_day[i]);
      Serial.print("temp.eve         : "); Serial.println(daily->temp_eve[i]);
      Serial.print("temp.night       : "); Serial.println(daily->temp_night[i]);
      Serial.print("temp.min         : "); Serial.println(daily->temp_min[i]);
      Serial.print("temp.max         : "); Serial.println(daily->temp_max[i]);

      Serial.print("feels_like.morn  : "); Serial.println(daily->feels_like_morn[i]);
      Serial.print("feels_like.day   : "); Serial.println(daily->feels_like_day[i]);
      Serial.print("feels_like.eve   : "); Serial.println(daily->feels_like_eve[i]);
      Serial.print("feels_like.night : "); Serial.println(daily->feels_like_night[i]);

      Serial.print("pressure         : "); Serial.println(daily->pressure[i]);
      Serial.print("humidity         : "); Serial.println(daily->humidity[i]);
      Serial.print("dew_point        : "); Serial.println(daily->dew_point[i]);
      Serial.print("uvi              : "); Serial.println(daily->uvi[i]);
      Serial.print("clouds           : "); Serial.println(daily->clouds[i]);
      Serial.print("visibility       : "); Serial.println(daily->visibility[i]);
      Serial.print("wind_speed       : "); Serial.println(daily->wind_speed[i]);
      Serial.print("wind_gust        : "); Serial.println(daily->wind_gust[i]);
      Serial.print("wind_deg         : "); Serial.println(daily->wind_deg[i]);
      Serial.print("rain             : "); Serial.println(daily->rain[i]);
      Serial.print("snow             : "); Serial.println(daily->snow[i]);
      Serial.println();
      Serial.print("id               : "); Serial.println(daily->id[i]);
      Serial.print("main             : "); Serial.println(daily->main[i]);
      Serial.print("description      : "); Serial.println(daily->description[i]);
      Serial.print("icon             : "); Serial.println(daily->icon[i]);
      Serial.print("pop              : "); Serial.println(daily->pop[i]);

      Serial.println();
    }
   // currentWeatherData.moonphase = String(daily->moon_phase[0]);
  }

  // Delete to free up space and prevent fragmentation as strings change in length
  delete current;
  delete hourly;
  delete daily;
}

bool WeatherConsumeNtpSync(time_t *outEpoch)
{
    if (!g_ntpSynced) return false;

    g_ntpSynced = false;
    if (outEpoch) *outEpoch = g_ntpEpoch;
    return true;
}

static bool fetchCurrentWeatherHTTP(WeatherData& out)
{
    if (WiFi.status() != WL_CONNECTED) return false;

    WiFiClient client;
    HTTPClient http;

    // Build URL: current weather
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

    // Stream parse (NO big payload String)
    StaticJsonDocument<2048> doc;
    DeserializationError err = deserializeJson(doc, http.getStream());
    http.end();

    if (err) {
        Serial.printf("[Weather] JSON parse failed: %s\n", err.c_str());
        return false;
    }

    // Parse only what you actually use
    float temp = doc["main"]["temp"] | NAN;
    uint16_t id = doc["weather"][0]["id"] | 666;
    const char* desc = doc["weather"][0]["description"] | "Unknown";

    out.temperature = String(temp, 1) + "°C";
    out.condition = desc;
    out.id = id;

    // OpenWeather returns unix dt, sunrise, sunset
    out.dt = doc["dt"] | (unsigned long)time(nullptr);
    out.sunrise = strTime((time_t)(doc["sys"]["sunrise"] | 0));
    out.sunset  = strTime((time_t)(doc["sys"]["sunset"]  | 0));
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

// PUBLIC MODEL ACCESSOR FOR TIDE CURVE (no UI here)
bool WeatherManager_GetTideCurve(float*   heights,
                                 uint16_t maxSamples,
                                 uint16_t& outCount,
                                 time_t&   outFirstSampleUtc,
                                 uint32_t& outStepSeconds)
{
    if (!heights || maxSamples < 2) {
        Serial.println("[WeatherManager] GetTideCurve: invalid buffer");
        return false;
    }

    if (g_tideState.count < 2) {
        Serial.printf("[WeatherManager] GetTideCurve: not enough extremes (%u)\n",
                      (unsigned)g_tideState.count);
        return false;
    }

    bool ok = TideBuildSampleCurve(g_tideState,
                                   heights,
                                   maxSamples,
                                   outCount,
                                   outFirstSampleUtc,
                                   outStepSeconds);
    if (!ok) {
        Serial.println("[WeatherManager] GetTideCurve: TideBuildSampleCurve failed");
        return false;
    }

    Serial.printf("[WeatherManager] GetTideCurve: count=%u, step=%u s\n",
                  (unsigned)outCount,
                  (unsigned)outStepSeconds);


    return true;
}

void WeatherManager_MarkTideCurveDirty()
{
    s_tideCurveDirty = true;
}

// Returns true exactly once per update (and clears the flag)
bool WeatherManager_TakeTideCurveDirtyFlag()
{
    if (!s_tideCurveDirty) return false;
    s_tideCurveDirty = false;
    return true;
}