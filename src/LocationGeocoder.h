#pragma once

#include <Arduino.h>

static constexpr uint8_t LOCATION_GEOCODER_MAX_RESULTS = 5;

struct LocationGeocodeResult {
    String name;
    String state;
    String country;
    double lat = 0.0;
    double lon = 0.0;

    String displayName() const {
        String out = name;
        if (state.length() > 0) {
            out += ", ";
            out += state;
        }
        if (country.length() > 0) {
            out += ", ";
            out += country;
        }
        return out;
    }
};

bool LocationGeocoderSearch(const String& query,
                            const String& token,
                            LocationGeocodeResult* results,
                            uint8_t maxResults,
                            uint8_t& outCount);
