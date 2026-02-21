#pragma once

#include <time.h>
#include "tide.h"

enum class TideUpdateResult {
    Ok,
    SkippedRateLimit,
    TimeNotReady,
    NetworkError,
    HttpError,
    ParseError
};

class TideService {
public:
    TideService(const char* apiKey, double lat, double lng);

    // Call as often as you like. It self-throttles to 1 request / 3h.
    TideUpdateResult update(uint16_t horizonHours, TideState& outState);

    static constexpr uint32_t MIN_REQUEST_INTERVAL_SEC = 3 * 60 * 60;

     bool saveCachedState(const TideState& state);
    bool loadCachedState(TideState& state);

private:
    const char* _apiKey;
    double _lat;
    double _lng;

    bool canFetchNow(time_t nowUtc, uint32_t& lastFetchOut);
    void recordSuccessfulFetch(time_t nowUtc);
};
