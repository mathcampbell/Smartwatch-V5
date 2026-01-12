#include "TimeManager.h"

#include <Arduino.h>
#include <Wire.h>
#include <time.h>
#include <sys/time.h>

#include "SensorPCF85063.hpp"
#define I2C_SCL 10
#define I2C_SDA 11

static SensorPCF85063 rtc;

// IMPORTANT:
// Strongly recommended: store RTC as UTC, not local time.
// That way DST/timezone changes are purely a display/system TZ concern.
static constexpr bool RTC_STORES_UTC = true;

static bool rtc_datetime_sane(const RTC_DateTime &dt)
{
    const int y = (int)dt.getYear();
    const int mo = (int)dt.getMonth();
    const int d = (int)dt.getDay();
    const int h = (int)dt.getHour();
    const int mi = (int)dt.getMinute();
    const int s = (int)dt.getSecond();

    if (y < 2024 || y > 2100) return false;
    if (mo < 1 || mo > 12) return false;
    if (d < 1 || d > 31) return false;
    if (h < 0 || h > 23) return false;
    if (mi < 0 || mi > 59) return false;
    if (s < 0 || s > 59) return false;
    return true;
}

static time_t tm_to_epoch_utc(struct tm *t)
{
    // Save current TZ
    const char *old_tz = getenv("TZ");

    // Force UTC
    setenv("TZ", "UTC0", 1);
    tzset();

    time_t epoch = mktime(t);

    // Restore TZ
    if (old_tz) {
        setenv("TZ", old_tz, 1);
    } else {
        unsetenv("TZ");
    }
    tzset();

    return epoch;
}

static time_t rtc_to_epoch(const RTC_DateTime &dt)
{
    struct tm t {};
    t.tm_year = (int)dt.getYear() - 1900;
    t.tm_mon  = (int)dt.getMonth() - 1;
    t.tm_mday = (int)dt.getDay();
    t.tm_hour = (int)dt.getHour();
    t.tm_min  = (int)dt.getMinute();
    t.tm_sec  = (int)dt.getSecond();
    t.tm_isdst = 0;

    if (RTC_STORES_UTC) {
        return tm_to_epoch_utc(&t);
    } else {
        return mktime(&t);
    }
}

bool time_manager_begin()
{
    // Waveshare example does: rtc.begin(Wire, IIC_SDA, IIC_SCL)
    if (!rtc.begin(Wire, I2C_SDA, I2C_SCL)) {
        Serial.println("[RTC] PCF85063 not found (begin failed)");
        return false;
    }
    return true;
}

bool time_manager_bootstrap_system_time_from_rtc()
{
    RTC_DateTime dt = rtc.getDateTime();
    if (!rtc_datetime_sane(dt)) {
        Serial.println("[RTC] time not sane; not bootstrapping system time");
        return false;
    }

    time_t epoch = rtc_to_epoch(dt);
    if (epoch < 1704067200) { // 2024-01-01 00:00:00 UTC
        Serial.println("[RTC] epoch too small; not bootstrapping system time");
        return false;
    }

    struct timeval tv {};
    tv.tv_sec = epoch;
    tv.tv_usec = 0;

    if (settimeofday(&tv, nullptr) != 0) {
        Serial.println("[RTC] settimeofday failed");
        return false;
    }

    Serial.printf("[RTC] system time set from RTC: %04d-%02d-%02d %02d:%02d:%02d\n",
                  dt.getYear(), dt.getMonth(), dt.getDay(),
                  dt.getHour(), dt.getMinute(), dt.getSecond());
    return true;
}

bool time_manager_write_rtc_from_system_time()
{
    time_t now = time(nullptr);
    if (now < 1704067200) { // still not valid
        Serial.println("[RTC] system time not valid; not writing RTC");
        return false;
    }

    struct tm t {};
    if (RTC_STORES_UTC) {
        gmtime_r(&now, &t);
    } else {
        localtime_r(&now, &t);
    }

    const uint16_t year = (uint16_t)(t.tm_year + 1900);
    const uint8_t  month = (uint8_t)(t.tm_mon + 1);
    const uint8_t  day = (uint8_t)t.tm_mday;
    const uint8_t  hour = (uint8_t)t.tm_hour;
    const uint8_t  minute = (uint8_t)t.tm_min;
    const uint8_t  second = (uint8_t)t.tm_sec;

    rtc.setDateTime(year, month, day, hour, minute, second);

    Serial.printf("[RTC] RTC updated from system time: %04u-%02u-%02u %02u:%02u:%02u (%s)\n",
                  year, month, day, hour, minute, second,
                  RTC_STORES_UTC ? "UTC" : "LOCAL");
    return true;
}

bool time_manager_read_rtc_epoch(time_t *outEpoch)
{
    if (!outEpoch) return false;

    RTC_DateTime dt = rtc.getDateTime();
    if (!rtc_datetime_sane(dt)) return false;

    *outEpoch = rtc_to_epoch(dt); // your existing conversion (UTC/local depending on your choice)
    return true;
}