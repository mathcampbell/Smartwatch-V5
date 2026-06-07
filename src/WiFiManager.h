#pragma once
#include <stdint.h>
#include <stdbool.h>

// Use uint8_t because this state is read from UI code and loop code.
enum WifiMgrState : uint8_t {
    WIFI_MGR_OFF = 0,
    WIFI_MGR_IDLE,
    WIFI_MGR_CONNECTING,
    WIFI_MGR_CONNECTED,
    WIFI_MGR_FAILED
};

void wifi_manager_begin();

// Start an async connection attempt to a specific network.
bool wifi_manager_start_connect(const char* ssid, const char* password, uint32_t timeout_ms);

// Start an async connection attempt using SettingsManager::currentSettings.
// Tries known_wifi_networks in stored order first, then falls back to the
// legacy wifi_ssid/wifi_pass fields if they are not already in the list.
bool wifi_manager_start_known_networks(uint32_t timeout_ms_per_network);

// Call this frequently from loop() (e.g., every iteration)
void wifi_manager_tick();

// Abort / power down
void wifi_manager_disconnect(bool power_off = true);

WifiMgrState wifi_manager_state();
int8_t wifi_manager_rssi();
const char* wifi_manager_current_ssid();

// Helper
bool wifi_manager_is_connected();
