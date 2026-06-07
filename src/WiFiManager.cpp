#include "WiFiManager.h"
#include "SettingsManager.h"

#include <WiFi.h>
#include <Arduino.h>
#include <vector>

static volatile WifiMgrState g_state = WIFI_MGR_IDLE;
static volatile int8_t g_rssi = -127;

static uint32_t g_start_ms = 0;
static uint32_t g_timeout_ms = 0;
static uint32_t g_failed_since_ms = 0;
static constexpr uint32_t WIFI_FAILURE_VISIBLE_MS = 5000;

static String g_ssid;
static String g_pass;

static bool g_wifi_started = false;
static bool g_trying_known_networks = false;
static uint32_t g_known_timeout_ms = 20000;
static size_t g_known_index = 0;
static std::vector<WiFiNetwork> g_try_networks;

static bool network_exists_in_try_list(const String& ssid)
{
    for (const auto& n : g_try_networks) {
        if (n.ssid == ssid) return true;
    }
    return false;
}

static void ensure_wifi_started()
{
    if (!g_wifi_started) {
        WiFi.mode(WIFI_STA);
        WiFi.setSleep(false);
        g_wifi_started = true;
    }
}

static void enter_failed_state()
{
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    g_wifi_started = false;
    g_trying_known_networks = false;
    g_try_networks.clear();
    g_state = WIFI_MGR_FAILED;
    g_failed_since_ms = millis();
    g_rssi = -127;
}

static bool start_current_network()
{
    if (g_ssid.length() == 0) return false;

    ensure_wifi_started();

    WiFi.disconnect(false);
    delay(50);
    WiFi.begin(g_ssid.c_str(), g_pass.c_str());
    g_start_ms = millis();
    g_state = WIFI_MGR_CONNECTING;

    Serial.print("[WiFiMgr] Connecting to ");
    Serial.print(g_ssid);
    Serial.print(" timeout=");
    Serial.print(g_timeout_ms);
    Serial.println("ms");
    return true;
}

static bool start_known_index(size_t index)
{
    if (index >= g_try_networks.size()) return false;

    g_known_index = index;
    g_ssid = g_try_networks[g_known_index].ssid;
    g_pass = g_try_networks[g_known_index].password;
    g_timeout_ms = g_known_timeout_ms;
    return start_current_network();
}

void wifi_manager_begin() {
    g_state = WIFI_MGR_IDLE;
}

bool wifi_manager_start_connect(const char* ssid, const char* password, uint32_t timeout_ms) {
    if (!ssid || !ssid[0]) return false;

    g_trying_known_networks = false;
    g_try_networks.clear();

    if (WiFi.status() == WL_CONNECTED) {
        g_state = WIFI_MGR_CONNECTED;
        g_rssi = (int8_t)WiFi.RSSI();
        return true;
    }

    g_ssid = ssid;
    g_pass = password ? password : "";
    g_timeout_ms = timeout_ms;
    return start_current_network();
}

bool wifi_manager_start_known_networks(uint32_t timeout_ms_per_network)
{
    if (WiFi.status() == WL_CONNECTED) {
        g_state = WIFI_MGR_CONNECTED;
        g_rssi = (int8_t)WiFi.RSSI();
        return true;
    }

    g_try_networks.clear();

    for (const auto& network : currentSettings.known_wifi_networks) {
        if (network.ssid.length() > 0 && !network_exists_in_try_list(network.ssid)) {
            g_try_networks.push_back(network);
        }
    }

    if (currentSettings.wifi_ssid.length() > 0 && !network_exists_in_try_list(currentSettings.wifi_ssid)) {
        WiFiNetwork legacy;
        legacy.ssid = currentSettings.wifi_ssid;
        legacy.password = currentSettings.wifi_pass;
        g_try_networks.push_back(legacy);
    }

    if (g_try_networks.empty()) {
        Serial.println("[WiFiMgr] No remembered networks configured.");
        enter_failed_state();
        return false;
    }

    g_trying_known_networks = true;
    g_known_timeout_ms = timeout_ms_per_network;
    g_known_index = 0;

    return start_known_index(0);
}

void wifi_manager_tick() {
    if (g_state == WIFI_MGR_FAILED) {
        if ((millis() - g_failed_since_ms) >= WIFI_FAILURE_VISIBLE_MS) {
            g_state = WIFI_MGR_OFF;
        }
        return;
    }

    if (g_state != WIFI_MGR_CONNECTING) return;

    wl_status_t st = WiFi.status();

    if (st == WL_CONNECTED) {
        g_state = WIFI_MGR_CONNECTED;
        g_rssi = (int8_t)WiFi.RSSI();
        Serial.print("[WiFiMgr] Connected to ");
        Serial.print(WiFi.SSID());
        Serial.print(" RSSI=");
        Serial.println(g_rssi);
        return;
    }

    const uint32_t elapsed = millis() - g_start_ms;
    const bool timed_out = elapsed >= g_timeout_ms;

    if (!timed_out) {
        return;
    }

    Serial.print("[WiFiMgr] Timed out connecting to ");
    Serial.print(g_ssid);
    Serial.print(" after ");
    Serial.print(elapsed);
    Serial.print("ms, status=");
    Serial.println((int)st);

    if (g_trying_known_networks && (g_known_index + 1) < g_try_networks.size()) {
        start_known_index(g_known_index + 1);
        return;
    }

    enter_failed_state();
}

void wifi_manager_disconnect(bool power_off) {
    WiFi.disconnect(true);
    if (power_off) {
        WiFi.mode(WIFI_OFF);
        g_wifi_started = false;
    }
    g_trying_known_networks = false;
    g_try_networks.clear();
    g_state = WIFI_MGR_OFF;
    g_rssi = -127;
}

WifiMgrState wifi_manager_state() { return g_state; }
int8_t wifi_manager_rssi() { return g_rssi; }

const char* wifi_manager_current_ssid()
{
    return g_ssid.c_str();
}

bool wifi_manager_failure_visible()
{
    return g_state == WIFI_MGR_FAILED;
}

bool wifi_manager_is_connected() {
    return (WiFi.status() == WL_CONNECTED) || (g_state == WIFI_MGR_CONNECTED);
}
