#ifndef WEATHER_MANAGER_H
#define WEATHER_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <JSON_Decoder.h>
#include <OpenWeather.h>
#include <time.h>
#include <stdbool.h>
#include "Tide.h"

static constexpr uint8_t WEATHER_FORECAST_DAYS = 5;

struct WeatherData {
    String temperature;
    String condition;
    String icon;
    String sunrise;
    String sunset;
    String wind_speed;
    String humidity;
    String moonphase;
    String temp_min;
    String temp_max;
    unsigned long lastUpdate;
    uint16_t id;
    unsigned long dt;
};

struct WeatherForecastDay {
    String dayLabel;
    String temperature;
    String icon;
    uint16_t id;
    unsigned long dt;
};

extern String api_key;
extern String latitude;
extern String longitude;
extern String units;
extern String language;
extern WeatherData currentWeatherData;

void WeatherManagerBegin();
bool WeatherUpdate();
bool WeatherLoadCached();
bool ForecastLoadCached();
bool TideLoadCached();
const WeatherData& WeatherGet();
const WeatherForecastDay* WeatherForecastGet(uint8_t& outCount);

void WeatherInit();
void printCurrentWeather();
void updateWeatherData();
void updateForecastData();
const char* getMeteoconIcon(uint16_t id, bool today);
bool loadWeatherDataFromFile(const char* filePath, WeatherData& weather);
void saveWeatherDataToFile(const char* filePath, const WeatherData& weather);
bool loadForecastDataFromFile(const char* filePath);
void saveForecastDataToFile(const char* filePath);
void initializeWeatherData();
bool WeatherConsumeNtpSync(time_t *outEpoch);

String strTime(time_t unixTime);

const TideState& TideGet();
bool WeatherManager_GetTideCurve(float* heights,
                                 uint16_t maxSamples,
                                 uint16_t& outCount,
                                 time_t& outFirstSampleUtc,
                                 uint32_t& outStepSeconds);

void WeatherManager_MarkTideCurveDirty();
bool WeatherManager_TakeTideCurveDirtyFlag();

#endif
