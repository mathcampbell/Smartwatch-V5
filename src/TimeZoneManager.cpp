#include "TimeZoneManager.h"

#include <stdlib.h>
#include <time.h>

#include "SettingsManager.h"

bool timezone_manager_apply_offset_seconds(int32_t offsetSeconds)
{
    const int32_t offsetHours = offsetSeconds / 3600;
    const int32_t offsetMinutes = abs((int)(offsetSeconds % 3600)) / 60;

    String tz = "UTC";
    if (offsetSeconds == 0) {
        tz = "UTC0";
    } else {
        // POSIX TZ signs are inverted: UTC+1 is represented as UTC-1.
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

    Serial.print("[TimeZone] Applied offset seconds=");
    Serial.print(offsetSeconds);
    Serial.print(" TZ=");
    Serial.println(tz);
    return true;
}

bool timezone_manager_apply_from_settings()
{
    return timezone_manager_apply_offset_seconds(currentSettings.timezone_offset_seconds);
}
