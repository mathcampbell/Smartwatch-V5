#include "LocationGeocoder.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

static String s_lastError;

const String& LocationGeocoderLastError()
{
    return s_lastError;
}

static void setError(const String& msg)
{
    s_lastError = msg;
    Serial.print("[Geocoder] ");
    Serial.println(s_lastError);
}

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
    s_lastError = "";
    outCount = 0;

    if (!results || maxResults == 0) {
        setError("Invalid result buffer");
        return false;
    }

    String trimmedQuery = query;
    trimmedQuery.trim();

    if (trimmedQuery.length() < 2) {
        setError("Query too short");
        return false;
    }

    if (token.length() == 0) {
        setError("Weather API key missing");
        return false;
    }

    if (WiFi.status() != WL_CONNECTED) {
        setError("WiFi not connected");
        return false;
    }

    WiFiClient client;
    HTTPClient http;

    const uint8_t limit = maxResults > LOCATION_GEOCODER_MAX_RESULTS ? LOCATION_GEOCODER_MAX_RESULTS : maxResults;

    String url = "http://api.openweathermap.org/geo/1.0/direct?q=";
    url += urlEncode(trimmedQuery);
    url += "&limit=";
    url += String(limit);
    url += "&appid=";
    url += token;

    Serial.print("[Geocoder] Requesting location for: ");
    Serial.println(trimmedQuery);

    if (!http.begin(client, url)) {
        setError("HTTP begin failed");
        return false;
    }

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        String body = http.getString();
        http.end();
        String msg = "HTTP ";
        msg += String(code);
        if (body.length() > 0) {
            msg += ": ";
            msg += body.substring(0, 80);
        }
        setError(msg);
        return false;
    }

    StaticJsonDocument<4096> doc;
    DeserializationError err = deserializeJson(doc, http.getStream());
    http.end();

    if (err) {
        String msg = "JSON parse failed: ";
        msg += err.c_str();
        setError(msg);
        return false;
    }

    JsonArray arr = doc.as<JsonArray>();
    if (arr.isNull()) {
        setError("Response was not an array");
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

    if (outCount == 0) {
        setError("No matching locations");
        return false;
    }

    Serial.printf("[Geocoder] Found %u location result(s).\n", outCount);
    return true;
}
