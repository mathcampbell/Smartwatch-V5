#include "tide.h"
#include <Arduino.h>
#include <math.h>

float TidePhaseNow(const TideState& state, time_t nowUtc) {
    if (state.count < 2) return 0.0f;

    const TideExtreme* prev = nullptr;
    const TideExtreme* next = nullptr;

    for (size_t i = 0; i < state.count; ++i) {
        const TideExtreme& e = state.extremes[i];
        if (e.timeUtc <= nowUtc) {
            prev = &e;
        } else {
            next = &e;
            break;
        }
    }

    if (!prev || !next) return 0.0f;

    float total = float(next->timeUtc - prev->timeUtc);
    if (total <= 0.0f) return 0.0f;

    float elapsed = float(nowUtc - prev->timeUtc);
    float t = elapsed / total;

    // If we’re on a falling tide (high→low), flip so 0 always means low.
    if (prev->isHigh && !next->isHigh) {
        t = 1.0f - t;
    }

    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t;
}

bool TideBuildSampleCurve(const TideState& state,
                          float*           outHeights,
                          uint16_t         maxSamples,
                          uint16_t&        outCount,
                          time_t&          outFirstSampleUtc,
                          uint32_t&        outStepSeconds)
{
    if (state.count < 2) {
        Serial.printf("[Tide] TideBuildSampleCurve: state.count=%u (<2)\n",
                      static_cast<unsigned>(state.count));
        return false;
    }
    if (!outHeights || maxSamples < 2) {
        Serial.println("[Tide] TideBuildSampleCurve: invalid output buffer");
        return false;
    }

    const TideExtreme* extremes = state.extremes;
    time_t first = extremes[0].timeUtc;
    time_t last  = extremes[state.count - 1].timeUtc;

    if (last <= first) {
        Serial.printf("[Tide] TideBuildSampleCurve: non-positive span (first=%ld, last=%ld)\n",
                      static_cast<long>(first),
                      static_cast<long>(last));
        return false;
    }

    double spanSec = difftime(last, first);
    uint16_t targetSamples = maxSamples;
    const double minStepSec = 60.0; // don't bother with < 1-minute resolution

    double rawStep = spanSec / static_cast<double>(targetSamples - 1);
    if (rawStep < minStepSec) {
        targetSamples = static_cast<uint16_t>(spanSec / minStepSec) + 1;
        if (targetSamples < 2) targetSamples = 2;
        rawStep = spanSec / static_cast<double>(targetSamples - 1);
    }

    uint32_t stepSeconds = static_cast<uint32_t>(rawStep + 0.5); // round
    if (stepSeconds == 0) stepSeconds = 60;

    outFirstSampleUtc = first;
    outStepSeconds    = stepSeconds;
    outCount          = targetSamples;

    size_t seg = 0;
    for (uint16_t i = 0; i < outCount; ++i) {
        time_t t = outFirstSampleUtc + static_cast<time_t>(i) * static_cast<time_t>(stepSeconds);

        while (seg + 1 < state.count && state.extremes[seg + 1].timeUtc < t) {
            ++seg;
        }

        const TideExtreme& e0 = state.extremes[seg];
        const TideExtreme& e1 = (seg + 1 < state.count)
            ? state.extremes[seg + 1]
            : state.extremes[seg];

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

    Serial.printf("[Tide] TideBuildSampleCurve: span=%.0fs, step=%us, samples=%u\n",
                  spanSec,
                  static_cast<unsigned>(stepSeconds),
                  static_cast<unsigned>(outCount));

    return true;
}