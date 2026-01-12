#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

class AlarmManager {
public:
    static AlarmManager& instance();

    void begin();

    // Set alarm by hour/minute (24h). Enables alarm.
    void set(uint8_t hour24, uint8_t minute);

    void disable();
    bool isEnabled() const;

    // For UI
    void get(uint8_t &hour24, uint8_t &minute) const;

    // Call from loop (cheap). Suggested: 2–5 Hz, or 1 Hz.
    void tick();

    // Event consumption
    bool isRinging() const;
    void dismiss();                 // stop ringing, schedule next day (if still enabled)
    void snooze(uint16_t minutes);  // e.g. 10

    // One-shot “just triggered” flag for main to react once
    bool consumeTriggered();

private:
    AlarmManager() = default;

    // Stored alarm
    bool     enabled_ = false;
    uint16_t alarm_minutes_ = 0;     // 0..1439

    // Runtime state
    bool     ringing_ = false;
    bool     triggered_ = false;
    time_t   next_fire_epoch_ = 0;
    time_t   last_checked_epoch_ = 0;

    void recomputeNextFire_(time_t now);
};
