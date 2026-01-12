#pragma once
#include <stdint.h>
#include <stdbool.h>

enum WifiMgrState : uint8_t {
    WIFI_MGR_OFF = 0,
    WIFI_MGR_IDLE,
    WIFI_MGR_CONNECTING,
    WIFI_MGR_CONNECTED,
    WIFI_MGR_FAILED
};

void wifi_manager_begin();

// Start an async connection attempt (returns true if started)
bool wifi_manager_start_connect(const char* ssid, const char* password, uint32_t timeout_ms);

// Call this frequently from loop() (e.g., every iteration)
void wifi_manager_tick();

// Abort / power down
void wifi_manager_disconnect(bool power_off = true);

WifiMgrState wifi_manager_state();
int8_t wifi_manager_rssi();

// Helper
bool wifi_manager_is_connected();