#include "ui_SettingsLocation.h"

#include <Arduino.h>

#include "LocationGeocoder.h"
#include "SettingsManager.h"
#include "WeatherManager.h"
#include "WiFiManager.h"
#include "ui_KeyboardOverlay.h"

static lv_obj_t* s_locationTextarea = nullptr;
static lv_obj_t* s_resultsList = nullptr;
static lv_obj_t* s_coordsLabel = nullptr;
static lv_obj_t* s_statusLabel = nullptr;
static lv_timer_t* s_lookupTimer = nullptr;
static LocationGeocodeResult s_results[LOCATION_GEOCODER_MAX_RESULTS];
static uint8_t s_resultCount = 0;
static String s_pendingQuery;
static bool s_lookupPending = false;

static bool has_any_configured_wifi()
{
    if (!currentSettings.known_wifi_networks.empty()) return true;
    return currentSettings.wifi_ssid.length() > 0;
}

static void update_coords_label()
{
    if (!s_coordsLabel) return;

    if (currentSettings.weather_lat.length() == 0 || currentSettings.weather_long.length() == 0) {
        lv_label_set_text(s_coordsLabel, "Lat/Lon: not set");
        return;
    }

    String txt = "Lat: ";
    txt += currentSettings.weather_lat;
    txt += "\nLon: ";
    txt += currentSettings.weather_long;
    lv_label_set_text(s_coordsLabel, txt.c_str());
}

static void set_status(const char* text)
{
    if (s_statusLabel) lv_label_set_text(s_statusLabel, text ? text : "");
}

static void set_status_string(const String& text)
{
    if (s_statusLabel) lv_label_set_text(s_statusLabel, text.c_str());
}

static void clear_results()
{
    s_resultCount = 0;
    if (s_resultsList) {
        lv_obj_clean(s_resultsList);
        lv_obj_add_flag(s_resultsList, LV_OBJ_FLAG_HIDDEN);
    }
}

static void location_result_selected_cb(lv_event_t* e)
{
    const uintptr_t idx = (uintptr_t)lv_event_get_user_data(e);
    if (idx >= s_resultCount) return;

    const LocationGeocodeResult& r = s_results[idx];
    const String display = r.displayName();

    currentSettings.location_name = display;
    currentSettings.weather_lat = String(r.lat, 6);
    currentSettings.weather_long = String(r.lon, 6);

    if (s_locationTextarea) lv_textarea_set_text(s_locationTextarea, display.c_str());

    update_coords_label();
    clear_results();
    set_status("Location saved");

    saveSettingsDataToFile("/settings.json", currentSettings);
    WeatherManagerBegin();
    ui_keyboard_hide();
}

static void populate_results_list()
{
    if (!s_resultsList) return;
    lv_obj_clean(s_resultsList);

    for (uint8_t i = 0; i < s_resultCount; ++i) {
        String txt = s_results[i].displayName();
        lv_obj_t* btn = lv_list_add_btn(s_resultsList, LV_SYMBOL_GPS, txt.c_str());
        lv_obj_add_event_cb(btn, location_result_selected_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
    }

    if (s_resultCount > 0) lv_obj_clear_flag(s_resultsList, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_resultsList, LV_OBJ_FLAG_HIDDEN);
}

static void stop_lookup_timer()
{
    if (s_lookupTimer) {
        lv_timer_del(s_lookupTimer);
        s_lookupTimer = nullptr;
    }
}

static void perform_location_lookup()
{
    s_lookupPending = false;
    stop_lookup_timer();

    set_status("Searching...");

    uint8_t count = 0;
    const bool ok = LocationGeocoderSearch(s_pendingQuery,
                                           currentSettings.weather_api_key,
                                           s_results,
                                           LOCATION_GEOCODER_MAX_RESULTS,
                                           count);
    s_resultCount = ok ? count : 0;

    if (s_resultCount == 0) {
        clear_results();
        const String& err = LocationGeocoderLastError();
        set_status_string(err.length() > 0 ? err : String("No matching locations"));
        return;
    }

    set_status("Select a location");
    populate_results_list();
}

static void lookup_timer_cb(lv_timer_t* timer)
{
    (void)timer;
    wifi_manager_tick();

    if (!s_lookupPending) {
        stop_lookup_timer();
        return;
    }

    if (wifi_manager_is_connected()) {
        perform_location_lookup();
        return;
    }

    if (wifi_manager_state() == WIFI_MGR_FAILED) {
        s_lookupPending = false;
        stop_lookup_timer();
        set_status("No remembered WiFi reachable");
        return;
    }

    String msg = "Trying ";
    msg += wifi_manager_current_ssid();
    msg += "...";
    set_status_string(msg);
}

static void start_location_lookup(const String& query)
{
    s_pendingQuery = query;
    s_pendingQuery.trim();
    clear_results();

    if (s_pendingQuery.length() < 2) {
        set_status("");
        return;
    }

    if (currentSettings.weather_api_key.length() == 0) {
        set_status("Weather API key missing");
        return;
    }

    if (wifi_manager_is_connected()) {
        perform_location_lookup();
        return;
    }

    if (!has_any_configured_wifi()) {
        set_status("WiFi not configured");
        return;
    }

    set_status("Connecting WiFi...");
    s_lookupPending = true;
    wifi_manager_start_known_networks(15000);

    if (!s_lookupTimer) {
        s_lookupTimer = lv_timer_create(lookup_timer_cb, 250, nullptr);
    }
}

static void location_textarea_event_cb(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* ta = (lv_obj_t*)lv_event_get_target(e);

    if (code == LV_EVENT_FOCUSED) {
        set_status("Type a place name, then tap OK");
        return;
    }

    if (code != LV_EVENT_READY) return;

    ui_keyboard_hide();
    start_location_lookup(lv_textarea_get_text(ta));
}

void ui_settings_add_location_controls(lv_obj_t* parent)
{
    if (!parent) return;

    lv_obj_t* section = lv_label_create(parent);
    lv_label_set_text(section, "Weather / tides location");
    lv_obj_set_style_text_font(section, &lv_font_montserrat_12, 0);

    s_locationTextarea = lv_textarea_create(parent);
    lv_obj_set_width(s_locationTextarea, lv_pct(90));
    lv_textarea_set_one_line(s_locationTextarea, true);
    lv_textarea_set_placeholder_text(s_locationTextarea, "Town, city or postcode");
    lv_textarea_set_text(s_locationTextarea, currentSettings.location_name.c_str());
    lv_obj_add_event_cb(s_locationTextarea, location_textarea_event_cb, LV_EVENT_ALL, nullptr);
    ui_keyboard_attach_textarea(s_locationTextarea, parent);

    s_coordsLabel = lv_label_create(parent);
    lv_obj_set_width(s_coordsLabel, lv_pct(90));
    lv_obj_set_style_text_color(s_coordsLabel, lv_color_hex(0x777777), 0);
    lv_label_set_long_mode(s_coordsLabel, LV_LABEL_LONG_WRAP);
    update_coords_label();

    s_statusLabel = lv_label_create(parent);
    lv_obj_set_width(s_statusLabel, lv_pct(90));
    lv_obj_set_style_text_color(s_statusLabel, lv_color_hex(0x777777), 0);
    lv_label_set_text(s_statusLabel, "");

    s_resultsList = lv_list_create(parent);
    lv_obj_set_width(s_resultsList, lv_pct(90));
    lv_obj_set_height(s_resultsList, 110);
    lv_obj_add_flag(s_resultsList, LV_OBJ_FLAG_HIDDEN);
}
