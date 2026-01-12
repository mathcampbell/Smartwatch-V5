#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <sys/time.h>
#include <Arduino.h>

// Call once early in setup() after Wire.begin(...) (or pass Wire+pins inside begin)
bool time_manager_begin();

// If RTC contains a sane date/time, sets ESP32 system clock from it.
// Returns true if system time was set from RTC.
bool time_manager_bootstrap_system_time_from_rtc();

// Write current ESP32 system time into RTC.
// Call after NTP/time sync, and optionally before deep sleep.
bool time_manager_write_rtc_from_system_time();

bool time_manager_read_rtc_epoch(time_t *outEpoch);