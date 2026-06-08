#include "uiWeatherScreen.h"

#include "ui.h"
#include "ui_ClockScreen.h"
#include "ui_Settings.h"
#include "WeatherManager.h"
#include "SettingsManager.h"

#include <Arduino.h>
#include <lvgl.h>

static constexpr int16_t SCR_W = 466;
static constexpr int16_t SCR_H = 466;
static constexpr int16_t BG_SZ = 414;
static constexpr int16_t BG_X = (SCR_W - BG_SZ) / 2;
static constexpr int16_t BG_Y = 8;
static constexpr int16_t FRAME_W = 100;
static constexpr int16_t RING_W = 16;
static constexpr int16_t ARC_ROT = 130;
static constexpr int16_t ARC_SWEEP = 190;
static constexpr int16_t SEG_COUNT = 5;
static constexpr int16_t ARC_RANGE_MAX = 500;
static constexpr int16_t SEG_SIZE = ARC_RANGE_MAX / SEG_COUNT;
static constexpr int16_t SEG_GAP_DEG = 8;
static constexpr int16_t FORECAST_COUNT = WEATHER_FORECAST_DAYS;
static constexpr int16_t FORECAST_PANEL_X = 0;
static constexpr int16_t FORECAST_PANEL_Y = 315;
static constexpr int16_t FORECAST_PANEL_W = SCR_W;
static constexpr int16_t FORECAST_PANEL_H = SCR_H - FORECAST_PANEL_Y;
static constexpr int16_t FORECAST_CARD_W = 62;
static constexpr int16_t FORECAST_CARD_H = 78;
static constexpr int16_t FORECAST_ROW_Y = 10;
static constexpr int16_t FORECAST_START_X = 40;
static constexpr int16_t FORECAST_GAP = 12;

lv_obj_t* ui_WeatherScreen = nullptr;

static lv_obj_t* s_bg = nullptr;
static lv_obj_t* s_scrim = nullptr;
static lv_obj_t* s_frame = nullptr;
static lv_obj_t* s_arc = nullptr;
static lv_obj_t* s_locationLabel = nullptr;
static lv_obj_t* s_forecastPanel = nullptr;
static lv_obj_t* s_tempMain = nullptr;
static lv_obj_t* s_tempShadow = nullptr;
static lv_obj_t* s_condMain = nullptr;
static lv_obj_t* s_condShadow = nullptr;
static lv_obj_t* s_minmaxMain = nullptr;
static lv_obj_t* s_minmaxShadow = nullptr;
static lv_obj_t* s_detailMain = nullptr;
static lv_obj_t* s_detailShadow = nullptr;
static lv_obj_t* s_forecastCard[FORECAST_COUNT] = {};
static lv_obj_t* s_forecastDay[FORECAST_COUNT] = {};
static lv_obj_t* s_forecastIcon[FORECAST_COUNT] = {};
static lv_obj_t* s_forecastTemp[FORECAST_COUNT] = {};

static uint16_t s_lastId = 0xFFFF;
static unsigned long s_lastDt = 0;
static String s_lastIcon;
static String s_lastTemp;
static String s_lastCond;
static String s_lastMin;
static String s_lastMax;
static String s_lastHumidity;
static String s_lastWind;
static String s_lastSunrise;
static String s_lastSunset;
static String s_lastLocation;
static String s_lastForecastSignature;

static const char* pick_bg(uint16_t id, const String& icon);
static void set_shadow_label_text(lv_obj_t* shadow, lv_obj_t* main_lbl);
static void weather_arc_value_changed(lv_event_t* e);
static void weather_arc_released(lv_event_t* e);
static void weather_arc_draw(lv_event_t* e);
static void weather_frame_draw(lv_event_t* e);

static String whole_temp(const String& temp)
{
    if (temp.length() == 0) return "--°";
    int dot = temp.indexOf('.');
    if (dot > 0) {
        String out = temp.substring(0, dot);
        out += "°C";
        return out;
    }
    return temp;
}

static String short_time_from_ctime(const String& t)
{
    if (t.length() >= 16) return t.substring(11, 16);
    return t;
}

static String compact_location()
{
    String loc = currentSettings.location_name;
    loc.trim();
    if (loc.length() == 0) return "Location not set";
    int comma = loc.indexOf(',');
    if (comma > 0) loc = loc.substring(0, comma);
    loc.trim();
    return loc;
}

static String detail_line(const WeatherData& wd)
{
    String out;
    out.reserve(64);
    if (wd.humidity.length() > 0) {
        out += "Hum ";
        out += wd.humidity;
    }
    if (wd.wind_speed.length() > 0) {
        if (out.length()) out += " - ";
        out += "Wind ";
        out += wd.wind_speed;
    }
    String rise = short_time_from_ctime(wd.sunrise);
    String set = short_time_from_ctime(wd.sunset);
    if (rise.length() > 0 && set.length() > 0 && rise != "N/A" && set != "N/A") {
        if (out.length()) out += "\n";
        out += "Sun ";
        out += rise;
        out += " / ";
        out += set;
    }
    if (out.length() == 0) out = "Weather details unavailable";
    return out;
}

static String forecast_signature()
{
    uint8_t count = 0;
    const WeatherForecastDay* days = WeatherForecastGet(count);
    String sig;
    sig.reserve(96);
    sig += String(count);
    for (uint8_t i = 0; i < count && i < FORECAST_COUNT; ++i) {
        sig += '|';
        sig += days[i].dt;
        sig += ':';
        sig += days[i].temperature;
        sig += ':';
        sig += days[i].icon;
        sig += ':';
        sig += days[i].id;
    }
    return sig;
}

static void style_forecast_card(lv_obj_t* card)
{
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1A2733), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_pad_all(card, 4, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
}

static void set_forecast_slot(uint8_t i, const WeatherForecastDay* d)
{
    if (i >= FORECAST_COUNT) return;
    if (!d) {
        lv_label_set_text(s_forecastDay[i], "--");
        lv_image_set_src(s_forecastIcon[i], getMeteoconIcon(666, false));
        lv_label_set_text(s_forecastTemp[i], "--°");
        return;
    }
    lv_label_set_text(s_forecastDay[i], d->dayLabel.c_str());
    lv_image_set_src(s_forecastIcon[i], getMeteoconIcon(d->id, false));
    lv_label_set_text(s_forecastTemp[i], d->temperature.c_str());
}

void ui_WeatherScreen_screen_init(void)
{
    if(ui_WeatherScreen) return;
    Serial.println("[WeatherScreen] init start");

    ui_WeatherScreen = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_WeatherScreen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(ui_WeatherScreen, SCR_W, SCR_H);
    lv_obj_set_style_bg_color(ui_WeatherScreen, lv_color_hex(0x05070A), 0);
    lv_obj_set_style_bg_opa(ui_WeatherScreen, LV_OPA_COVER, 0);

    s_bg = lv_image_create(ui_WeatherScreen);
    lv_obj_set_size(s_bg, BG_SZ, BG_SZ);
    lv_obj_set_pos(s_bg, BG_X, BG_Y);
    lv_image_set_src(s_bg, "A:/lvgl/weather/cloudy-bg.jpg");

    s_scrim = lv_obj_create(ui_WeatherScreen);
    lv_obj_remove_style_all(s_scrim);
    lv_obj_set_size(s_scrim, BG_SZ, BG_SZ);
    lv_obj_set_pos(s_scrim, BG_X, BG_Y);
    lv_obj_set_style_radius(s_scrim, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_scrim, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_scrim, LV_OPA_30, 0);
    lv_obj_clear_flag(s_scrim, LV_OBJ_FLAG_SCROLLABLE);

    s_frame = lv_obj_create(ui_WeatherScreen);
    lv_obj_remove_style_all(s_frame);
    lv_obj_set_size(s_frame, SCR_W, SCR_H);
    lv_obj_set_pos(s_frame, 0, 0);
    lv_obj_clear_flag(s_frame, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_frame, weather_frame_draw, LV_EVENT_DRAW_MAIN, NULL);

    s_forecastPanel = lv_obj_create(ui_WeatherScreen);
    lv_obj_remove_style_all(s_forecastPanel);
    lv_obj_set_size(s_forecastPanel, FORECAST_PANEL_W, FORECAST_PANEL_H);
    lv_obj_set_pos(s_forecastPanel, FORECAST_PANEL_X, FORECAST_PANEL_Y);
    lv_obj_set_style_radius(s_forecastPanel, 0, 0);
    lv_obj_set_style_bg_color(s_forecastPanel, lv_color_hex(0x151C26), 0);
    lv_obj_set_style_bg_opa(s_forecastPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_forecastPanel, 0, 0);
    lv_obj_clear_flag(s_forecastPanel, LV_OBJ_FLAG_SCROLLABLE);

    s_arc = lv_arc_create(ui_WeatherScreen);
    lv_obj_set_size(s_arc, SCR_W, SCR_H);
    lv_obj_center(s_arc);
    lv_arc_set_rotation(s_arc, ARC_ROT);
    lv_arc_set_bg_angles(s_arc, 0, ARC_SWEEP);
    lv_arc_set_range(s_arc, 0, SEG_COUNT - 1);
    lv_arc_set_value(s_arc, SEG_COUNT - 1);
    lv_obj_clear_flag(s_arc, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_arc_opa(s_arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(s_arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_arc, 0, 0);
    lv_obj_add_event_cb(s_arc, weather_arc_value_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_arc, weather_arc_released, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_arc, weather_arc_released, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_arc, weather_arc_draw, LV_EVENT_DRAW_MAIN, NULL);

    s_locationLabel = lv_label_create(ui_WeatherScreen);
    lv_obj_set_width(s_locationLabel, 310);
    lv_obj_set_style_text_color(s_locationLabel, lv_color_hex(0xD8E8F8), 0);
    lv_obj_set_style_text_font(s_locationLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(s_locationLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_locationLabel, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(s_locationLabel, compact_location().c_str());
    lv_obj_align(s_locationLabel, LV_ALIGN_TOP_MID, 0, 40);

    static lv_style_t style_shadow;
    static bool shadow_inited = false;
    if(!shadow_inited) {
        shadow_inited = true;
        lv_style_init(&style_shadow);
        lv_style_set_text_opa(&style_shadow, LV_OPA_40);
        lv_style_set_text_color(&style_shadow, lv_color_black());
    }

    s_tempShadow = lv_label_create(ui_WeatherScreen);
    lv_obj_add_style(s_tempShadow, &style_shadow, 0);
    lv_obj_set_style_text_font(s_tempShadow, &lv_font_montserrat_48, 0);

    s_tempMain = lv_label_create(ui_WeatherScreen);
    lv_obj_set_style_text_color(s_tempMain, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_tempMain, &lv_font_montserrat_48, 0);
    lv_label_set_text(s_tempMain, "--°");
    lv_obj_align(s_tempMain, LV_ALIGN_CENTER, 0, -92);
    set_shadow_label_text(s_tempShadow, s_tempMain);
    lv_obj_align_to(s_tempShadow, s_tempMain, LV_ALIGN_TOP_LEFT, 2, 2);

    s_condShadow = lv_label_create(ui_WeatherScreen);
    lv_obj_add_style(s_condShadow, &style_shadow, 0);
    lv_obj_set_style_text_font(s_condShadow, &lv_font_montserrat_22, 0);
    lv_obj_set_width(s_condShadow, 320);
    lv_obj_set_style_text_align(s_condShadow, LV_TEXT_ALIGN_CENTER, 0);

    s_condMain = lv_label_create(ui_WeatherScreen);
    lv_obj_set_style_text_color(s_condMain, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_condMain, &lv_font_montserrat_22, 0);
    lv_obj_set_width(s_condMain, 320);
    lv_obj_set_style_text_align(s_condMain, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_condMain, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(s_condMain, "Weather");
    lv_obj_align_to(s_condMain, s_tempMain, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);
    set_shadow_label_text(s_condShadow, s_condMain);
    lv_obj_align_to(s_condShadow, s_condMain, LV_ALIGN_TOP_LEFT, 2, 2);

    s_minmaxShadow = lv_label_create(ui_WeatherScreen);
    lv_obj_add_style(s_minmaxShadow, &style_shadow, 0);
    lv_obj_set_style_text_font(s_minmaxShadow, &lv_font_montserrat_18, 0);

    s_minmaxMain = lv_label_create(ui_WeatherScreen);
    lv_obj_set_style_text_color(s_minmaxMain, lv_color_hex(0xD8E8F8), 0);
    lv_obj_set_style_text_font(s_minmaxMain, &lv_font_montserrat_18, 0);
    lv_label_set_text(s_minmaxMain, "--° / --°");
    lv_obj_align_to(s_minmaxMain, s_condMain, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);
    set_shadow_label_text(s_minmaxShadow, s_minmaxMain);
    lv_obj_align_to(s_minmaxShadow, s_minmaxMain, LV_ALIGN_TOP_LEFT, 2, 2);

    s_detailShadow = lv_label_create(ui_WeatherScreen);
    lv_obj_add_style(s_detailShadow, &style_shadow, 0);
    lv_obj_set_style_text_font(s_detailShadow, &lv_font_montserrat_12, 0);
    lv_obj_set_width(s_detailShadow, 350);
    lv_obj_set_style_text_align(s_detailShadow, LV_TEXT_ALIGN_CENTER, 0);

    s_detailMain = lv_label_create(ui_WeatherScreen);
    lv_obj_set_style_text_color(s_detailMain, lv_color_hex(0xC3D7E8), 0);
    lv_obj_set_style_text_font(s_detailMain, &lv_font_montserrat_12, 0);
    lv_obj_set_width(s_detailMain, 350);
    lv_obj_set_style_text_align(s_detailMain, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_detailMain, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_detailMain, "");
    lv_obj_align_to(s_detailMain, s_minmaxMain, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);
    set_shadow_label_text(s_detailShadow, s_detailMain);
    lv_obj_align_to(s_detailShadow, s_detailMain, LV_ALIGN_TOP_LEFT, 1, 1);

    for (uint8_t i = 0; i < FORECAST_COUNT; ++i) {
        const int16_t x = FORECAST_START_X + i * (FORECAST_CARD_W + FORECAST_GAP);
        s_forecastCard[i] = lv_obj_create(s_forecastPanel);
        lv_obj_set_size(s_forecastCard[i], FORECAST_CARD_W, FORECAST_CARD_H);
        lv_obj_set_pos(s_forecastCard[i], x, FORECAST_ROW_Y);
        style_forecast_card(s_forecastCard[i]);
        s_forecastDay[i] = lv_label_create(s_forecastCard[i]);
        lv_obj_set_style_text_color(s_forecastDay[i], lv_color_hex(0xB9D7F2), 0);
        lv_obj_set_style_text_font(s_forecastDay[i], &lv_font_montserrat_12, 0);
        lv_label_set_text(s_forecastDay[i], "--");
        lv_obj_align(s_forecastDay[i], LV_ALIGN_TOP_MID, 0, 2);
        s_forecastIcon[i] = lv_image_create(s_forecastCard[i]);
        lv_obj_set_size(s_forecastIcon[i], 32, 32);
        lv_image_set_src(s_forecastIcon[i], getMeteoconIcon(666, false));
        lv_obj_align(s_forecastIcon[i], LV_ALIGN_CENTER, 0, -2);
        s_forecastTemp[i] = lv_label_create(s_forecastCard[i]);
        lv_obj_set_style_text_color(s_forecastTemp[i], lv_color_white(), 0);
        lv_obj_set_style_text_font(s_forecastTemp[i], &lv_font_montserrat_16, 0);
        lv_label_set_text(s_forecastTemp[i], "--°");
        lv_obj_align(s_forecastTemp[i], LV_ALIGN_BOTTOM_MID, 0, -2);
    }

    lv_obj_move_foreground(s_frame);
    lv_obj_move_foreground(s_tempShadow);
    lv_obj_move_foreground(s_tempMain);
    lv_obj_move_foreground(s_condShadow);
    lv_obj_move_foreground(s_condMain);
    lv_obj_move_foreground(s_minmaxShadow);
    lv_obj_move_foreground(s_minmaxMain);
    lv_obj_move_foreground(s_detailShadow);
    lv_obj_move_foreground(s_detailMain);
    lv_obj_move_foreground(s_forecastPanel);
    lv_obj_move_foreground(s_arc);
    lv_obj_move_foreground(s_locationLabel);

    lv_obj_send_event(s_arc, LV_EVENT_VALUE_CHANGED, NULL);
    s_lastId = 0xFFFF;
    s_lastDt = 0;
    s_lastForecastSignature = "";
    ui_WeatherScreen_tick();
    Serial.println("[WeatherScreen] init complete");
}

void ui_WeatherScreen_tick(void)
{
    if(!ui_WeatherScreen) return;
    if(lv_screen_active() != ui_WeatherScreen) return;
    const WeatherData& wd = WeatherGet();
    const String forecastSig = forecast_signature();
    const String loc = compact_location();
    const bool changed = (wd.id != s_lastId) || (wd.dt != s_lastDt) || (wd.icon != s_lastIcon) || (wd.temperature != s_lastTemp) || (wd.condition != s_lastCond) || (wd.temp_min != s_lastMin) || (wd.temp_max != s_lastMax) || (wd.humidity != s_lastHumidity) || (wd.wind_speed != s_lastWind) || (wd.sunrise != s_lastSunrise) || (wd.sunset != s_lastSunset) || (loc != s_lastLocation) || (forecastSig != s_lastForecastSignature);
    if(!changed) return;
    s_lastId = wd.id;
    s_lastDt = wd.dt;
    s_lastIcon = wd.icon;
    s_lastTemp = wd.temperature;
    s_lastCond = wd.condition;
    s_lastMin = wd.temp_min;
    s_lastMax = wd.temp_max;
    s_lastHumidity = wd.humidity;
    s_lastWind = wd.wind_speed;
    s_lastSunrise = wd.sunrise;
    s_lastSunset = wd.sunset;
    s_lastLocation = loc;
    s_lastForecastSignature = forecastSig;
    if(s_locationLabel) lv_label_set_text(s_locationLabel, loc.c_str());
    if(s_bg) lv_image_set_src(s_bg, pick_bg(wd.id, wd.icon));
    if(s_tempMain && s_tempShadow) {
        lv_label_set_text(s_tempMain, whole_temp(wd.temperature).c_str());
        set_shadow_label_text(s_tempShadow, s_tempMain);
        lv_obj_align(s_tempMain, LV_ALIGN_CENTER, 0, -92);
        lv_obj_align_to(s_tempShadow, s_tempMain, LV_ALIGN_TOP_LEFT, 2, 2);
    }
    if(s_condMain && s_condShadow) {
        lv_label_set_text(s_condMain, wd.condition.length() ? wd.condition.c_str() : "Weather");
        set_shadow_label_text(s_condShadow, s_condMain);
        lv_obj_align_to(s_condMain, s_tempMain, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);
        lv_obj_align_to(s_condShadow, s_condMain, LV_ALIGN_TOP_LEFT, 2, 2);
    }
    if(s_minmaxMain && s_minmaxShadow) {
        String minmax = wd.temp_min;
        if(minmax.length() == 0) minmax = "--°";
        minmax += " / ";
        minmax += wd.temp_max.length() ? wd.temp_max : "--°";
        lv_label_set_text(s_minmaxMain, minmax.c_str());
        set_shadow_label_text(s_minmaxShadow, s_minmaxMain);
        lv_obj_align_to(s_minmaxMain, s_condMain, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);
        lv_obj_align_to(s_minmaxShadow, s_minmaxMain, LV_ALIGN_TOP_LEFT, 2, 2);
    }
    if(s_detailMain && s_detailShadow) {
        String details = detail_line(wd);
        lv_label_set_text(s_detailMain, details.c_str());
        set_shadow_label_text(s_detailShadow, s_detailMain);
        lv_obj_align_to(s_detailMain, s_minmaxMain, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);
        lv_obj_align_to(s_detailShadow, s_detailMain, LV_ALIGN_TOP_LEFT, 1, 1);
    }
    uint8_t count = 0;
    const WeatherForecastDay* days = WeatherForecastGet(count);
    for (uint8_t i = 0; i < FORECAST_COUNT; ++i) set_forecast_slot(i, (i < count) ? &days[i] : nullptr);
}

static void weather_arc_value_changed(lv_event_t* e)
{
    lv_obj_t* arc = lv_event_get_target_obj(e);
    int v = (int)lv_arc_get_value(arc);
    if(v < 0) v = 0;
    if(v >= SEG_COUNT) v = SEG_COUNT - 1;
    if(lv_arc_get_value(arc) != v) {
        lv_arc_set_value(arc, v);
        return;
    }
    lv_obj_invalidate(arc);
}

static void weather_arc_released(lv_event_t* e)
{
    lv_obj_t* arc = lv_event_get_target_obj(e);
    int seg = (int)lv_arc_get_value(arc);
    switch(seg) {
        case 0: _ui_screen_change(&ui_MainScreen, LV_SCR_LOAD_ANIM_NONE, 0, 0, ui_MainScreen_screen_init); break;
        case 1: _ui_screen_change(&ui_ClockScreen, LV_SCR_LOAD_ANIM_NONE, 0, 0, ui_ClockScreen_screen_init); break;
        case 2: _ui_screen_change(&ui_MusicControls, LV_SCR_LOAD_ANIM_NONE, 0, 0, ui_MusicControls_screen_init); break;
        case 3: _ui_screen_change(&ui_Settings, LV_SCR_LOAD_ANIM_NONE, 0, 0, ui_Settings_screen_init); break;
        case 4:
        default: return;
    }
}

static const char* pick_bg(uint16_t id, const String& icon)
{
    const bool night = (icon.length() >= 3 && icon.charAt(2) == 'n');
    if(id == 800) return night ? "A:/lvgl/weather/clear-night-bg.jpg" : "A:/lvgl/weather/clear-day-bg.jpg";
    if(id == 801) return night ? "A:/lvgl/weather/patchy-night-bg.jpg" : "A:/lvgl/weather/patchy-day-bg.jpg";
    if(id == 802 || id == 803 || id == 804) return "A:/lvgl/weather/cloudy-bg.jpg";
    if(id / 100 == 2) return "A:/lvgl/weather/thunder-bg.jpg";
    if(id / 100 == 3) return "A:/lvgl/weather/drizzle-bg.jpg";
    if(id / 100 == 5) return (id <= 501) ? "A:/lvgl/weather/light-rain-bg.jpg" : "A:/lvgl/weather/rain-bg.jpg";
    if(id / 100 == 6) return (id >= 611 && id <= 616) ? "A:/lvgl/weather/sleet-bg.jpg" : "A:/lvgl/weather/snow-bg.jpg";
    if(id / 100 == 7) return "A:/lvgl/weather/fog-bg.jpg";
    return "A:/lvgl/weather/cloudy-bg.jpg";
}

static void set_shadow_label_text(lv_obj_t* shadow, lv_obj_t* main_lbl)
{
    if(!shadow || !main_lbl) return;
    const char* t = lv_label_get_text(main_lbl);
    if(!t) t = "";
    lv_label_set_text(shadow, t);
}

static void weather_frame_draw(lv_event_t* e)
{
    lv_layer_t* layer = lv_event_get_layer(e);
    if(!layer) return;
    const int32_t cx = BG_X + (BG_SZ / 2);
    const int32_t cy = BG_Y + (BG_SZ / 2);
    const int32_t inner_radius = BG_SZ / 2;
    lv_draw_arc_dsc_t frame;
    lv_draw_arc_dsc_init(&frame);
    frame.center.x = (lv_coord_t)cx;
    frame.center.y = (lv_coord_t)cy;
    frame.radius = (lv_coord_t)(inner_radius + (FRAME_W / 2));
    frame.width = FRAME_W;
    frame.start_angle = 0;
    frame.end_angle = 360;
    frame.opa = LV_OPA_COVER;
    frame.color = lv_color_hex(0x05070A);
    frame.rounded = 0;
    lv_draw_arc(layer, &frame);
}

static void weather_arc_draw(lv_event_t* e)
{
    lv_obj_t* obj = lv_event_get_target_obj(e);
    lv_layer_t* layer = lv_event_get_layer(e);
    if(!layer) return;
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    const int32_t w = lv_area_get_width(&a);
    const int32_t h = lv_area_get_height(&a);
    const int32_t cx = a.x1 + w / 2;
    const int32_t cy = a.y1 + h / 2;
    const int32_t r = (LV_MIN(w, h) / 2) - (RING_W / 2) - 1;
    const int sel = (int)lv_arc_get_value(obj);
    lv_draw_arc_dsc_t base;
    lv_draw_arc_dsc_init(&base);
    base.center.x = (lv_coord_t)cx;
    base.center.y = (lv_coord_t)cy;
    base.radius = (lv_coord_t)r;
    base.width = (lv_coord_t)RING_W;
    base.opa = LV_OPA_80;
    base.color = lv_color_hex(0x0B111A);
    base.rounded = 0;
    lv_draw_arc_dsc_t hi = base;
    hi.opa = LV_OPA_COVER;
    hi.color = lv_color_hex(0x2A9DFF);
    const int32_t seg_span = ARC_SWEEP / SEG_COUNT;
    const int32_t gap = SEG_GAP_DEG;
    for(int i = 0; i < SEG_COUNT; i++) {
        int32_t start = ARC_ROT + (i * seg_span) + (gap / 2);
        int32_t end = ARC_ROT + ((i + 1) * seg_span) - (gap / 2);
        base.start_angle = start;
        base.end_angle = end;
        lv_draw_arc(layer, &base);
        if(i == sel) {
            hi.start_angle = start;
            hi.end_angle = end;
            lv_draw_arc(layer, &hi);
        }
    }
}
