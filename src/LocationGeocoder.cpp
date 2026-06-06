#include "LocationGeocoder.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

static String urlEncode(const String& in)
{
    String out;
    out.reserve(in.length() * 3);

    const char* hex = "0123456789ABCDEF";
    for (size_t i = 0; i < in.length(); ++i) {
        const char c = in[i];
        if ((c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out += c;
        } else if (c == ' ') {
            out += "%20";
        } else {
            out += '%';
            out += hex[(c >> 4) & 0x0F];
            out += hex[c & 0x0F];
        }
    }

    return out;
}

bool LocationGeocoderSearch(const String& query,
                            const String& token,
                            LocationGeocodeResult* results,
                            uint8_t maxResults,
                            uint8_t& outCount)
{
    outCount = 0;

    if (!results || maxResults == 0) {
        Serial.println("[Geocoder] Invalid result buffer.");
        return false;
    }

    if (query.length() < 2) {
        Serial.println("[Geocoder] Query too short.");
        return false;
    }

    if (token.length() == 0) {
        Serial.println("[Geocoder] Missing OpenWeather token.");
        return false;
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[Geocoder] WiFi not connected.");
        return false;
    }

    WiFiClient client;
    HTTPClient http;

    const uint8_t limit = maxResults > LOCATION_GEOCODER_MAX_RESULTS ? LOCATION_GEOCODER_MAX_RESULTS : maxResults;

    String url = "http://api.openweathermap.org/geo/1.0/direct?q=";
    url += urlEncode(query);
    url += "&limit=";
    url += String(limit);
    url += "&appid=";
    url += token;

    if (!http.begin(client, url)) {
        Serial.println("[Geocoder] http.begin failed.");
        return false;
    }

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("[Geocoder] HTTP GET failed: %d\n", code);
        http.end();
        return false;
    }

    StaticJsonDocument<4096> doc;
    DeserializationError err = deserializeJson(doc, http.getStream());
    http.end();

    if (err) {
        Serial.printf("[Geocoder] JSON parse failed: %s\n", err.c_str());
        return false;
    }

    JsonArray arr = doc.as<JsonArray>();
    if (arr.isNull()) {
        Serial.println("[Geocoder] Response was not an array.");
        return false;
    }

    for (JsonObject obj : arr) {
        if (outCount >= limit) break;

        LocationGeocodeResult& r = results[outCount];
        r.name = obj["name"].as<String>();
        r.state = obj["state"].as<String>();
        r.country = obj["country"].as<String>();
        r.lat = obj["lat"] | 0.0;
        r.lon = obj["lon"] | 0.0;

        if (r.name.length() > 0) {
            outCount++;
        }
    }

    Serial.printf("[Geocoder] Found %u location result(s).\n", outCount);
    return outCount > 0;
}
