#include "AlarmManager.h"

AlarmManager& AlarmManager::instance() {
    static AlarmManager inst;
    return inst;
}

void AlarmManager::begin() {
    enabled_ = false;
    ringing_ = false;
    triggered_ = false;
    next_fire_epoch_ = 0;
    last_checked_epoch_ = 0;
}

void AlarmManager::set(uint8_t hour24, uint8_t minute) {
    if (hour24 > 23) hour24 = 23;
    if (minute > 59) minute = 59;

    alarm_minutes_ = (uint16_t)hour24 * 60u + (uint16_t)minute;
    enabled_ = true;
    ringing_ = false;
    triggered_ = false;

    time_t now = time(nullptr);
    recomputeNextFire_(now);
}

void AlarmManager::disable() {
    enabled_ = false;
    ringing_ = false;
    triggered_ = false;
    next_fire_epoch_ = 0;
}

bool AlarmManager::isEnabled() const { return enabled_; }


void AlarmManager::get(uint8_t &hour24, uint8_t &minute) const {
    hour24 = (uint8_t)(alarm_minutes_ / 60u);
    minute = (uint8_t)(alarm_minutes_ % 60u);
}

bool AlarmManager::isRinging() const { return ringing_; }

bool AlarmManager::consumeTriggered() {
    if (!triggered_) return false;
    triggered_ = false;
    return true;
}

void AlarmManager::dismiss() {
    ringing_ = false;
    triggered_ = false;
    if (!enabled_) return;

    time_t now = time(nullptr);
    recomputeNextFire_(now);
}

void AlarmManager::snooze(uint16_t minutes) {
    if (!ringing_) return;
    if (minutes == 0) minutes = 10;

    time_t now = time(nullptr);
    next_fire_epoch_ = now + (time_t)minutes * 60;
    ringing_ = false;          // stop current ring
    triggered_ = false;
    enabled_ = true;           // still enabled
}

void AlarmManager::tick() {
    if (!enabled_) return;

    time_t now = time(nullptr);
    if (now <= 0) return;  // time not set yet

    // Don’t do expensive work more than once per second
    if (now == last_checked_epoch_) return;
    last_checked_epoch_ = now;

    // If next_fire not computed yet (e.g. time became valid)
    if (next_fire_epoch_ == 0) {
        recomputeNextFire_(now);
        return;
    }

    if (!ringing_ && now >= next_fire_epoch_) {
        ringing_ = true;
        triggered_ = true;

        // Schedule the next daily alarm immediately, so dismiss doesn’t need to.
        // Keeps behavior stable across screen changes.
        recomputeNextFire_(now + 1);
    }
}

void AlarmManager::recomputeNextFire_(time_t now) {
    if (!enabled_) { next_fire_epoch_ = 0; return; }

    struct tm t;
    localtime_r(&now, &t);

    // Construct today’s alarm time in localtime
    t.tm_hour = (int)(alarm_minutes_ / 60u);
    t.tm_min  = (int)(alarm_minutes_ % 60u);
    t.tm_sec  = 0;

    time_t candidate = mktime(&t);  // local time -> epoch

    // If today’s time already passed, schedule for tomorrow
    if (candidate <= now) {
        candidate += 24 * 60 * 60;
    }

    next_fire_epoch_ = candidate;
}
