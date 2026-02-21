#include "TideService.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <stdlib.h>   // getenv, setenv, unsetenv
#include <time.h>     // tzset, difftime

  

static constexpr const char* PREF_NS          = "tide";
static constexpr const char* KEY_LAST_FETCH   = "lastFetchUtc";
static constexpr time_t      TIME_VALID_CUTOFF = 1'600'000'000; // ~2020

// How many samples we push into the UI (must be <= TIDE_MAX_SAMPLES in ui_MainScreen.cpp)
static constexpr uint16_t TIDE_UI_MAX_SAMPLES = 96;

static constexpr const char* TIDE_CACHE_PATH    = "/tide.json";

TideService::TideService(const char* apiKey, double lat, double lng)
: _apiKey(apiKey), _lat(lat), _lng(lng) {}

// -----------------------------------------------------------------------------
// Tide cache persistence (LittleFS)
// -----------------------------------------------------------------------------

static bool loadTideStateFromFile(const char* path, TideState& state)
{
    File file = LittleFS.open(path, FILE_READ);
    if (!file) {
        Serial.printf("[TideService] loadTideStateFromFile: failed to open %s\n", path);
        state.count        = 0;
        state.fetchedAtUtc = 0;
        return false;
    }

    DynamicJsonDocument doc(1024); // enough for a small extremes array
    DeserializationError err = deserializeJson(doc, file);
    file.close();

    if (err) {
        Serial.print("[TideService] loadTideStateFromFile: JSON error: ");
        Serial.println(err.c_str());
        state.count        = 0;
        state.fetchedAtUtc = 0;
        return false;
    }

    uint32_t fetched = doc["fetchedAtUtc"] | 0;
    state.fetchedAtUtc = (time_t)fetched;

    JsonArray arr = doc["extremes"].as<JsonArray>();
    if (arr.isNull()) {
        Serial.println("[TideService] loadTideStateFromFile: no extremes[] array");
        state.count = 0;
        return false;
    }

    size_t i = 0;
    for (JsonObject obj : arr) {
        if (i >= TideState::MAX_EXTREMES) break;

        TideExtreme& e = state.extremes[i++];
        e.timeUtc = (time_t)(obj["timeUtc"] | 0);
        e.height  = obj["height"]  | 0.0f;
        e.isHigh  = obj["isHigh"]  | false;
    }
    state.count = i;

    Serial.printf("[TideService] loadTideStateFromFile: loaded %u extremes, fetchedAtUtc=%lu\n",
                  (unsigned)state.count,
                  (unsigned long)state.fetchedAtUtc);

    return state.count >= 2;
}

static bool saveTideStateToFile(const char* path, const TideState& state)
{
    File file = LittleFS.open(path, FILE_WRITE);
    if (!file) {
        Serial.printf("[TideService] saveTideStateToFile: failed to open %s for writing\n", path);
        return false;
    }

    DynamicJsonDocument doc(1024);

    doc["fetchedAtUtc"] = (uint32_t)state.fetchedAtUtc;

    JsonArray arr = doc.createNestedArray("extremes");
    for (size_t i = 0; i < state.count && i < TideState::MAX_EXTREMES; ++i) {
        const TideExtreme& e = state.extremes[i];
        JsonObject obj       = arr.createNestedObject();
        obj["timeUtc"] = (uint32_t)e.timeUtc;
        obj["height"]  = e.height;
        obj["isHigh"]  = e.isHigh;
    }

    size_t written = serializeJson(doc, file);
    file.close();

    if (written == 0) {
        Serial.println("[TideService] saveTideStateToFile: serializeJson wrote 0 bytes");
        return false;
    }

    Serial.printf("[TideService] saveTideStateToFile: wrote %u extremes to %s\n",
                  (unsigned)state.count,
                  path);
    return true;
}



// Decide whether we’re *allowed* to hit the API now.
bool TideService::canFetchNow(time_t nowUtc, uint32_t& lastFetchOut) {
    if (nowUtc < TIME_VALID_CUTOFF) {
        Serial.printf("[TideService] canFetchNow: nowUtc=%ld < cutoff=%ld -> false\n",
                      static_cast<long>(nowUtc),
                      static_cast<long>(TIME_VALID_CUTOFF));
        lastFetchOut = 0;
        return false;
    }

    Preferences prefs;
    if (!prefs.begin(PREF_NS, false)) {
        Serial.println("[TideService] canFetchNow: prefs.begin() failed, treating as never fetched");
        lastFetchOut = 0;
        return true;
    }

    lastFetchOut = prefs.getUInt(KEY_LAST_FETCH, 0);
    prefs.end();

    if (lastFetchOut == 0) {
        Serial.println("[TideService] canFetchNow: no previous fetch, allowed immediately");
        return true;  // never fetched before
    }

    uint32_t elapsed = static_cast<uint32_t>(nowUtc - static_cast<time_t>(lastFetchOut));
    Serial.printf("[TideService] canFetchNow: lastFetch=%u, elapsed=%u s, minInterval=%u s\n",
                  static_cast<unsigned>(lastFetchOut),
                  static_cast<unsigned>(elapsed),
                  static_cast<unsigned>(MIN_REQUEST_INTERVAL_SEC));

    return elapsed >= MIN_REQUEST_INTERVAL_SEC;
}


// Build a regular time grid of tide heights from discrete extremes.
// Returns false if there isn't enough data to build a curve.
static bool buildTideSamplesFromExtremes(
    const TideState& state,
    float*           outHeights,
    uint16_t         maxSamples,
    uint16_t&        outCount,
    time_t&          outFirstSampleUtc,
    uint32_t&        outStepSeconds)
{
    if (state.count < 2) {
        Serial.printf("[TideService] buildTideSamplesFromExtremes: state.count=%u (<2)\n",
                      static_cast<unsigned>(state.count));
        return false;
    }
    if (!outHeights || maxSamples < 2) {
        Serial.println("[TideService] buildTideSamplesFromExtremes: invalid output buffer");
        return false;
    }

    const TideExtreme* extremes = state.extremes;
    time_t first = extremes[0].timeUtc;
    time_t last  = extremes[state.count - 1].timeUtc;
    if (last <= first) {
        Serial.printf("[TideService] buildTideSamplesFromExtremes: non-positive span (first=%ld, last=%ld)\n",
                      static_cast<long>(first), static_cast<long>(last));
        return false;
    }

    double spanSec = difftime(last, first);
    // Start with our global max; if that implies <60s spacing, reduce.
    uint16_t targetSamples = maxSamples;
    double minStep = 60.0; // don't bother with < 1-minute resolution

    double rawStep = spanSec / static_cast<double>(targetSamples - 1);
    if (rawStep < minStep) {
        targetSamples = static_cast<uint16_t>(spanSec / minStep) + 1;
        if (targetSamples < 2) targetSamples = 2;
        rawStep = spanSec / static_cast<double>(targetSamples - 1);
    }

    uint32_t stepSeconds = static_cast<uint32_t>(rawStep + 0.5); // round
    if (stepSeconds == 0) stepSeconds = 60;

    outFirstSampleUtc = first;
    outStepSeconds    = stepSeconds;
    outCount          = targetSamples;

    size_t k = 0;
    for (uint16_t i = 0; i < outCount; ++i) {
        time_t t = outFirstSampleUtc + static_cast<time_t>(i) * static_cast<time_t>(stepSeconds);

        // Find the segment [extremes[k], extremes[k+1]] that covers t
        while (k + 1 < state.count && state.extremes[k + 1].timeUtc < t) {
            ++k;
        }

        const TideExtreme& e0 = state.extremes[k];
        const TideExtreme& e1 = (k + 1 < state.count) ? state.extremes[k + 1] : state.extremes[k];

        if (e1.timeUtc == e0.timeUtc) {
            outHeights[i] = e0.height;
        } else {
            double f = static_cast<double>(t - e0.timeUtc) /
                       static_cast<double>(e1.timeUtc - e0.timeUtc);
            if (f < 0.0) f = 0.0;
            if (f > 1.0) f = 1.0;
            outHeights[i] = static_cast<float>(e0.height + f * (e1.height - e0.height));
        }
    }

    Serial.printf("[TideService] buildTideSamplesFromExtremes: span=%.0fs, step=%us, samples=%u\n",
                  spanSec,
                  static_cast<unsigned>(stepSeconds),
                  static_cast<unsigned>(outCount));
    return true;
}

// Take the current TideState and push a curve into the main screen UI.



// Persist the fact that we succeeded just now.
void TideService::recordSuccessfulFetch(time_t nowUtc) {
    if (nowUtc < TIME_VALID_CUTOFF) {
        Serial.printf("[TideService] recordSuccessfulFetch: nowUtc=%ld < cutoff, ignoring\n",
                      static_cast<long>(nowUtc));
        return;
    }

    Preferences prefs;
    if (!prefs.begin(PREF_NS, false)) {
        Serial.println("[TideService] recordSuccessfulFetch: prefs.begin() failed");
        return;
    }

    uint32_t stored = static_cast<uint32_t>(nowUtc);
    prefs.putUInt(KEY_LAST_FETCH, stored);
    prefs.end();

    Serial.printf("[TideService] recordSuccessfulFetch: stored lastFetchUtc=%lu\n",
                  static_cast<unsigned long>(stored));
}


TideUpdateResult TideService::update(uint16_t horizonHours, TideState& outState) {
    time_t nowUtc = time(nullptr);
    Serial.printf("[TideService] update() called at %ld (UTC), horizon=%u h\n",
                  static_cast<long>(nowUtc),
                  static_cast<unsigned>(horizonHours));

    if (nowUtc < TIME_VALID_CUTOFF) {
        Serial.printf("[TideService] TimeNotReady: nowUtc=%ld < cutoff=%ld\n",
                      static_cast<long>(nowUtc),
                      static_cast<long>(TIME_VALID_CUTOFF));
        return TideUpdateResult::TimeNotReady;
    }

    uint32_t lastFetch = 0;
    bool allowed = canFetchNow(nowUtc, lastFetch);
    Serial.printf("[TideService] canFetchNow -> %s (lastFetch=%u)\n",
                  allowed ? "true" : "false",
                  static_cast<unsigned>(lastFetch));

if (!allowed) {
    uint32_t elapsed = (lastFetch == 0)
        ? 0U
        : static_cast<uint32_t>(nowUtc - static_cast<time_t>(lastFetch));
    Serial.printf("[TideService] SkippedRateLimit: elapsed=%u s, minInterval=%u s\n",
                  static_cast<unsigned>(elapsed),
                  static_cast<unsigned>(MIN_REQUEST_INTERVAL_SEC));

    // Try to rehydrate outState from tide.json cache
    if (loadTideStateFromFile(TIDE_CACHE_PATH, outState) && outState.count >= 2) {
        Serial.printf("[TideService] SkippedRateLimit: loaded %u cached extremes from %s\n",
                      static_cast<unsigned>(outState.count),
                      TIDE_CACHE_PATH);

        // We have valid cached data, no need to hit the API
        return TideUpdateResult::SkippedRateLimit;
    }

    // No valid cache: override the rate limit once and fall through to HTTP fetch
    Serial.println("[TideService] SkippedRateLimit: no valid cache; overriding rate limit and fetching now");
    // Note: no 'return' here – we continue into the network code below.
}

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[TideService] NetworkError: WiFi not connected");
        return TideUpdateResult::NetworkError;
    }

// Construct Stormglass URL with some history so the curve starts in the past
const uint16_t HISTORY_HOURS = 12;

time_t startUtc = nowUtc - static_cast<time_t>(HISTORY_HOURS) * 3600;
if (startUtc < TIME_VALID_CUTOFF) {
    startUtc = TIME_VALID_CUTOFF;
}

time_t endUtc = nowUtc + static_cast<time_t>(horizonHours) * 3600;

String url = "https://api.stormglass.io/v2/tide/extremes/point?";
url += "lat=";
url += String(_lat, 6);
url += "&lng=";
url += String(_lng, 6);
url += "&start=";
url += String(static_cast<uint32_t>(startUtc));
url += "&end=";
url += String(static_cast<uint32_t>(endUtc));
url += "&datum=MSL";

    Serial.print("[TideService] Requesting URL: ");
    Serial.println(url);

    WiFiClientSecure client;
    client.setInsecure(); // TODO: cert if you want full TLS verification

    HTTPClient https;
    if (!https.begin(client, url)) {
        Serial.println("[TideService] https.begin() failed");
        return TideUpdateResult::NetworkError;
    }

    https.addHeader("Authorization", _apiKey);
    int httpCode = https.GET();

    Serial.printf("[TideService] HTTP GET returned: %d\n", httpCode);

    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("[TideService] HTTP error: %d\n", httpCode);
        https.end();
        return TideUpdateResult::HttpError;
    }

    String payload = https.getString();
    https.end();

    Serial.printf("[TideService] Payload size: %u bytes\n",
                  static_cast<unsigned>(payload.length()));

    StaticJsonDocument<8 * 1024> doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        Serial.print("[TideService] JSON parse error: ");
        Serial.println(err.c_str());
        return TideUpdateResult::ParseError;
    }

    JsonArray data = doc["data"].as<JsonArray>();
    if (data.isNull()) {
        Serial.println("[TideService] No data[] array in JSON");
        return TideUpdateResult::ParseError;
    }

    Serial.printf("[TideService] JSON data[] size: %u\n",
                  static_cast<unsigned>(data.size()));

    size_t count = 0;
    size_t skippedBadTime       = 0;
    size_t skippedMissingFields = 0;

    for (JsonObject obj : data) {
        if (count >= TideState::MAX_EXTREMES) break;

        const char* typeStr = obj["type"];   // "high" / "low"
        const char* timeStr = obj["time"];   // ISO8601
        float height = obj["height"] | 0.0f;

        if (!typeStr || !timeStr) {
            ++skippedMissingFields;
            continue;
        }

        struct tm t = {};
        if (sscanf(timeStr, "%4d-%2d-%2dT%2d:%2d:%2d",
                   &t.tm_year, &t.tm_mon, &t.tm_mday,
                   &t.tm_hour, &t.tm_min, &t.tm_sec) != 6) {
            ++skippedBadTime;
            continue;
        }
        t.tm_year -= 1900;
        t.tm_mon  -= 1;

        // Convert ISO8601 to epoch in UTC (ESP32 tz dance)
        char* oldTZ = getenv("TZ");
        setenv("TZ", "UTC0", 1);
        tzset();

        time_t ts = mktime(&t);

        if (oldTZ) {
            setenv("TZ", oldTZ, 1);
        } else {
            unsetenv("TZ");
        }
        tzset();

        if (ts <= 0) {
            ++skippedBadTime;
            continue;
        }

        TideExtreme& e = outState.extremes[count++];
        e.timeUtc = ts;
        e.height  = height;
        e.isHigh  = (strcmp(typeStr, "high") == 0);
    }

   outState.count        = count;
outState.fetchedAtUtc = nowUtc;

Serial.printf("[TideService] Parsed %u extremes (skipped: badTime=%u, missing=%u)\n",
              static_cast<unsigned>(count),
              static_cast<unsigned>(skippedBadTime),
              static_cast<unsigned>(skippedMissingFields));

if (count < 2) {
    Serial.println("[TideService] Not enough extremes to be useful (need >= 2)");
    return TideUpdateResult::ParseError;
}

// Keep using Preferences for rate-limiting
recordSuccessfulFetch(nowUtc);

// Persist tide state to its own cache file
if (!saveTideStateToFile(TIDE_CACHE_PATH, outState)) {
    Serial.println("[TideService] Warning: failed to persist tide state to tide.json");
}

Serial.printf("[TideService] Parsed %u extremes, recorded fetch time, and saved cache\n",
              static_cast<unsigned>(count));

// NOTE: no UI calls here. Caller decides what to do with outState.
return TideUpdateResult::Ok;
}
