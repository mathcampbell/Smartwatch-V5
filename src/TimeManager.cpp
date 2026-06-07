#include "TimeManager.h"

#include <Arduino.h>
#include <Wire.h>
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>

#include "SensorPCF85063.hpp"
#define I2C_SCL 10
#define I2C_SDA 11

static SensorPCF85063 rtc;
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
    const char *old_tz_ptr = getenv("TZ");
    String old_tz = old_tz_ptr ? String(old_tz_ptr) : String();
    const bool had_old_tz = old_tz_ptr != nullptr;

    setenv("TZ", "UTC0", 1);
    tzset();

    time_t epoch = mktime(t);

    if (had_old_tz) {
        setenv("TZ", old_tz.c_str(), 1);
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
    if (epoch < 1704067200) {
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
    if (now < 1704067200) {
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

    *outEpoch = rtc_to_epoch(dt);
    return true;
}

bool time_manager_apply_timezone_offset_seconds(int32_t offsetSeconds)
{
    const int32_t offsetHours = offsetSeconds / 3600;
    const int32_t offsetMinutes = abs((int)(offsetSeconds % 3600)) / 60;

    String tz = "UTC";
    if (offsetSeconds == 0) {
        tz = "UTC0";
    } else {
        tz += (offsetSeconds > 0) ? "-" : "+";
        tz += String(abs((int)offsetHours));
        if (offsetMinutes > 0) {
            tz += ":";
            if (offsetMinutes < 10) tz += "0";
            tz += String(offsetMinutes);
        }
    }

    setenv("TZ", tz.c_str(), 1);
    tzset();

    Serial.print("[TimeManager] Applied timezone offset seconds=");
    Serial.print(offsetSeconds);
    Serial.print(" TZ=");
    Serial.println(tz);
    return true;
}
