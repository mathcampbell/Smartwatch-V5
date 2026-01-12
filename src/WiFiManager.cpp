#include "WiFiManager.h"
#include <WiFi.h>
#include <Arduino.h>

static volatile WifiMgrState g_state = WIFI_MGR_IDLE;
static volatile int8_t g_rssi = -127;

static uint32_t g_start_ms = 0;
static uint32_t g_timeout_ms = 0;

static const char* g_ssid = nullptr;
static const char* g_pass = nullptr;

// Optional: prevent repeated begin() spam
static bool g_wifi_started = false;


void wifi_manager_begin() {
    g_state = WIFI_MGR_IDLE;
}

bool wifi_manager_start_connect(const char* ssid, const char* password, uint32_t timeout_ms) {
    if (!ssid || !ssid[0]) return false;

    // If already connected, don't restart.
    if (WiFi.status() == WL_CONNECTED) {
        g_state = WIFI_MGR_CONNECTED;
        g_rssi = (int8_t)WiFi.RSSI();
        return true;
    }

    g_ssid = ssid;
    g_pass = password;
    g_timeout_ms = timeout_ms;
    g_start_ms = millis();

    if (!g_wifi_started) {
        WiFi.mode(WIFI_STA);
        g_wifi_started = true;
    }

    // Kick off the connect attempt (returns immediately)
    WiFi.begin(g_ssid, g_pass);
    g_state = WIFI_MGR_CONNECTING;

    return true;
}

void wifi_manager_tick() {
    if (g_state != WIFI_MGR_CONNECTING) return;

    wl_status_t st = WiFi.status();

    if (st == WL_CONNECTED) {
        g_state = WIFI_MGR_CONNECTED;
        g_rssi = (int8_t)WiFi.RSSI();
        return;
    }

    // If it hard-failed fast
    if (st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL) {
        g_state = WIFI_MGR_FAILED;
        g_rssi = -127;
        return;
    }

    // Timeout?
    if ((millis() - g_start_ms) >= g_timeout_ms) {
        // stop trying and (optionally) power down
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        g_wifi_started = false;

        g_state = WIFI_MGR_FAILED;
        g_rssi = -127;
        return;
    }

    // Otherwise: still connecting; do nothing.
}

void wifi_manager_disconnect(bool power_off) {
    WiFi.disconnect(true);
    if (power_off) {
        WiFi.mode(WIFI_OFF);
        g_wifi_started = false;
    }
    g_state = WIFI_MGR_OFF;
    g_rssi = -127;
}

WifiMgrState wifi_manager_state() { return g_state; }
int8_t wifi_manager_rssi() { return g_rssi; }

bool wifi_manager_is_connected() {
    return (WiFi.status() == WL_CONNECTED) || (g_state == WIFI_MGR_CONNECTED);
}