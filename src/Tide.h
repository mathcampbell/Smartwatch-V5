// tide.h
#pragma once
#include <time.h>
#include <stddef.h>

struct TideExtreme {
    time_t timeUtc;   // UTC time of the high/low
    float  height;    // metres
    bool   isHigh;    // true = high, false = low
};

struct TideState {
    static constexpr size_t MAX_EXTREMES = 16;

    TideExtreme extremes[MAX_EXTREMES];
    size_t count = 0;

    time_t fetchedAtUtc = 0;
};

// Helper to get [0..1] “phase” between low (0) and high (1)
float TidePhaseNow(const TideState& state, time_t nowUtc);

// Build a regular time grid of tide heights from discrete extremes.
//  - Returns false if there isn't enough data or parameters are invalid.
//  - outCount, outFirstSampleUtc and outStepSeconds are set on success.
bool TideBuildSampleCurve(const TideState& state,
                          float*           outHeights,
                          uint16_t         maxSamples,
                          uint16_t&        outCount,
                          time_t&          outFirstSampleUtc,
                          uint32_t&        outStepSeconds);