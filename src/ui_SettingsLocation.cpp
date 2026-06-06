#include "ui_SettingsLocation.h"

#include <Arduino.h>

#include "LocationGeocoder.h"
#include "SettingsManager.h"
#include "WeatherManager.h"

static lv_obj_t* s_locationTextarea = nullptr;
static lv_obj_t* s_resultsList = nullptr;
static lv_obj_t* s_coordsLabel = nullptr;
static lv_obj_t* s_statusLabel = nullptr;
static LocationGeocodeResult s_results[LOCATION_GEOCODER_MAX_RESULTS];
static uint8_t s_resultCount = 0;

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

static void location_textarea_event_cb(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* ta = (lv_obj_t*)lv_event_get_target(e);

    if (code == LV_EVENT_FOCUSED) {
        set_status("Type a place name");
        return;
    }

    if (code != LV_EVENT_READY && code != LV_EVENT_VALUE_CHANGED) return;

    const String query = lv_textarea_get_text(ta);
    if (query.length() < 2) {
        clear_results();
        set_status("");
        return;
    }

    set_status("Searching...");

    uint8_t count = 0;
    const bool ok = LocationGeocoderSearch(query,
                                           currentSettings.weather_api_key,
                                           s_results,
                                           LOCATION_GEOCODER_MAX_RESULTS,
                                           count);
    s_resultCount = ok ? count : 0;

    if (s_resultCount == 0) {
        clear_results();
        set_status("No matching locations");
        return;
    }

    set_status("Select a location");
    populate_results_list();
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
