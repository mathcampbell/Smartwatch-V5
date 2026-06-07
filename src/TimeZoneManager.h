#pragma once

#include <Arduino.h>
#include <stdint.h>

// Applies a fixed local offset, in seconds east of UTC, to localtime().
// The system epoch and RTC remain UTC; this only changes displayed local time.
bool timezone_manager_apply_offset_seconds(int32_t offsetSeconds);

// Applies currentSettings.timezone_offset_seconds if present/non-zero.
bool timezone_manager_apply_from_settings();
