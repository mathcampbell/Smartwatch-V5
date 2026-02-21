#include "uiWeatherScreen.h"

#include "ui.h"                 // ui_MainScreen, ui_ClockScreen, etc + _ui_screen_change
#include "ui_ClockScreen.h"
//#include "ui_MusicControls.c"
#include "ui_Settings.h"

#include "WeatherManager.h"

#include <Arduino.h>
#include <lvgl.h>

// ---------- Screen / layout constants ----------
static constexpr int16_t SCR_W = 466;
static constexpr int16_t SCR_H = 466;

// Background image is now smaller to speed decode + leave room for nav ring
static constexpr int16_t BG_SZ = 350;

// Ring band thickness (visual)
static constexpr int16_t RING_W = 58;     // ring stroke width
static constexpr int16_t IND_W  = 18;     // indicator stroke width

// Arc geometry: 300° sweep like your other screens
static constexpr int16_t ARC_ROT = 120;   // rotation
static constexpr int16_t ARC_SWEEP = 300; // background arc angles 0..300

// Segments: 5
static constexpr int16_t SEG_COUNT = 5;
static constexpr int16_t ARC_RANGE_MAX = 500;
static constexpr int16_t SEG_SIZE = ARC_RANGE_MAX / SEG_COUNT; // 100
static constexpr int16_t SEG_GAP_DEG = 6;                 // gap between segments (degrees)
static constexpr int16_t SEG_SPAN_DEG = ARC_SWEEP / SEG_COUNT; // 60 for 300°/5


// ---------- LVGL objects ----------
lv_obj_t* ui_WeatherScreen = nullptr;

static lv_obj_t* s_bg = nullptr;           // background image (350x350)
static lv_obj_t* s_arc = nullptr;          // outer arc menu
static lv_obj_t* s_selLabel = nullptr;     // selection label (optional)

static lv_obj_t* s_tempMain = nullptr;
static lv_obj_t* s_tempShadow = nullptr;

static lv_obj_t* s_condMain = nullptr;
static lv_obj_t* s_condShadow = nullptr;

static lv_obj_t* s_minmaxMain = nullptr;
static lv_obj_t* s_minmaxShadow = nullptr;

// Change detection (so we don’t spam setters)
static uint16_t s_lastId = 0xFFFF;
static unsigned long s_lastDt = 0;
static String s_lastIcon;
static String s_lastTemp;
static String s_lastCond;

// ---------- Helpers ----------
static const char* pick_bg(uint16_t id, const String& icon);
static const char* pick_label_for_arc_value(int v);
static void set_shadow_label_text(lv_obj_t* shadow, lv_obj_t* main_lbl);

// ---------- Arc callbacks ----------
static void weather_arc_value_changed(lv_event_t* e);
static void weather_arc_released(lv_event_t* e);
static void weather_arc_draw(lv_event_t* e);

void ui_WeatherScreen_screen_init(void)
{
    if(ui_WeatherScreen) return;

    ui_WeatherScreen = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_WeatherScreen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(ui_WeatherScreen, SCR_W, SCR_H);

    // Base styling: dark surround
    lv_obj_set_style_bg_color(ui_WeatherScreen, lv_color_hex(0x05070A), 0);
    lv_obj_set_style_bg_opa(ui_WeatherScreen, LV_OPA_COVER, 0);

    // --- Background image (centered, smaller) ---
    s_bg = lv_image_create(ui_WeatherScreen);
    lv_obj_set_size(s_bg, BG_SZ, BG_SZ);
    lv_obj_center(s_bg);

    // A default so the screen isn’t blank at boot
    lv_image_set_src(s_bg, "A:/lvgl/weather/cloudy-bg.jpg");

    // --- Outer ring arc menu ---
    s_arc = lv_arc_create(ui_WeatherScreen);
    lv_obj_set_size(s_arc, SCR_W, SCR_H);
    lv_obj_center(s_arc);

    lv_arc_set_rotation(s_arc, ARC_ROT);
    lv_arc_set_bg_angles(s_arc, 0, ARC_SWEEP);
    // Use discrete logical range: 0..4 (one per segment)
    lv_arc_set_range(s_arc, 0, SEG_COUNT - 1);

    // Start on “Weather” (segment 4)
    lv_arc_set_value(s_arc, SEG_COUNT - 1);

    // Make it a “selector” – we don’t want a knob, and we don’t want scrolling
    lv_obj_clear_flag(s_arc, LV_OBJ_FLAG_SCROLLABLE);

    // Hide knob
// Keep arc for touch input, but we custom-draw everything.
lv_obj_set_style_arc_opa(s_arc, LV_OPA_TRANSP, LV_PART_MAIN);
lv_obj_set_style_arc_opa(s_arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
lv_obj_set_style_bg_opa(s_arc, LV_OPA_TRANSP, LV_PART_KNOB);
lv_obj_set_style_pad_all(s_arc, 0, 0);

    // Ring styling
    lv_obj_set_style_arc_color(s_arc, lv_color_hex(0x0C1118), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(s_arc, LV_OPA_COVER, LV_PART_MAIN);

    // Indicator styling (brighter)
    lv_obj_set_style_arc_color(s_arc, lv_color_hex(0x2A9DFF), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(s_arc, LV_OPA_COVER, LV_PART_INDICATOR);

    // Optional: remove knob completely (some themes still show it unless you kill its opa)
    lv_obj_set_style_bg_opa(s_arc, LV_OPA_TRANSP, LV_PART_KNOB);

    // Events
    lv_obj_add_event_cb(s_arc, weather_arc_value_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_arc, weather_arc_released, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_arc, weather_arc_released, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_arc, weather_arc_draw, LV_EVENT_DRAW_MAIN, NULL);


    // Label showing current selection (small, in the ring area)
    s_selLabel = lv_label_create(ui_WeatherScreen);
    lv_obj_set_style_text_color(s_selLabel, lv_color_hex(0x9FB3C8), 0);
    lv_obj_set_style_text_font(s_selLabel, &lv_font_montserrat_16, 0);
    lv_label_set_text(s_selLabel, "Weather");
    lv_obj_align(s_selLabel, LV_ALIGN_TOP_MID, 0, 20);

    // --- Temp label + fake shadow ---
    // Shadow style
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

    lv_obj_align(s_tempMain, LV_ALIGN_CENTER, 0, -50);
    set_shadow_label_text(s_tempShadow, s_tempMain);
    lv_obj_align_to(s_tempShadow, s_tempMain, LV_ALIGN_TOP_LEFT, 2, 2);

    // --- Condition label + shadow ---
    s_condShadow = lv_label_create(ui_WeatherScreen);
    lv_obj_add_style(s_condShadow, &style_shadow, 0);
    lv_obj_set_style_text_font(s_condShadow, &lv_font_montserrat_22, 0);

    s_condMain = lv_label_create(ui_WeatherScreen);
    lv_obj_set_style_text_color(s_condMain, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_condMain, &lv_font_montserrat_22, 0);
    lv_label_set_text(s_condMain, "Weather");

    lv_obj_align_to(s_condMain, s_tempMain, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    set_shadow_label_text(s_condShadow, s_condMain);
    lv_obj_align_to(s_condShadow, s_condMain, LV_ALIGN_TOP_LEFT, 2, 2);

    // --- Min/max label + shadow (placeholder) ---
    s_minmaxShadow = lv_label_create(ui_WeatherScreen);
    lv_obj_add_style(s_minmaxShadow, &style_shadow, 0);
    lv_obj_set_style_text_font(s_minmaxShadow, &lv_font_montserrat_18, 0);

    s_minmaxMain = lv_label_create(ui_WeatherScreen);
    lv_obj_set_style_text_color(s_minmaxMain, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_minmaxMain, &lv_font_montserrat_18, 0);
    lv_label_set_text(s_minmaxMain, "--° / --°");

    lv_obj_align_to(s_minmaxMain, s_condMain, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);
    set_shadow_label_text(s_minmaxShadow, s_minmaxMain);
    lv_obj_align_to(s_minmaxShadow, s_minmaxMain, LV_ALIGN_TOP_LEFT, 2, 2);

    // Prime the selector label immediately
    lv_obj_send_event(s_arc, LV_EVENT_VALUE_CHANGED, NULL);
}

void ui_WeatherScreen_tick(void)
{
    if(!ui_WeatherScreen) return;
    if(lv_screen_active() != ui_WeatherScreen) return;

    const WeatherData& wd = WeatherGet();

    const bool changed =
        (wd.id != s_lastId) ||
        (wd.dt != s_lastDt) ||
        (wd.icon != s_lastIcon) ||
        (wd.temperature != s_lastTemp) ||
        (wd.condition != s_lastCond);

    if(!changed) return;

    s_lastId   = wd.id;
    s_lastDt   = wd.dt;
    s_lastIcon = wd.icon;
    s_lastTemp = wd.temperature;
    s_lastCond = wd.condition;

    // Background
    if(s_bg) {
        const char* path = pick_bg(wd.id, wd.icon);
        lv_image_set_src(s_bg, path);
    }

    // Temp
    if(s_tempMain && s_tempShadow) {
        lv_label_set_text(s_tempMain, wd.temperature.c_str());
        set_shadow_label_text(s_tempShadow, s_tempMain);
        lv_obj_align(s_tempMain, LV_ALIGN_CENTER, 0, -50);
        lv_obj_align_to(s_tempShadow, s_tempMain, LV_ALIGN_TOP_LEFT, 2, 2);
    }

    // Condition
    if(s_condMain && s_condShadow) {
        lv_label_set_text(s_condMain, wd.condition.c_str());
        set_shadow_label_text(s_condShadow, s_condMain);
        lv_obj_align_to(s_condMain, s_tempMain, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
        lv_obj_align_to(s_condShadow, s_condMain, LV_ALIGN_TOP_LEFT, 2, 2);
    }

    // Min/max placeholder (swap later when you have real fields)
    if(s_minmaxMain && s_minmaxShadow) {
        lv_label_set_text(s_minmaxMain, "--° / --°");
        set_shadow_label_text(s_minmaxShadow, s_minmaxMain);
        lv_obj_align_to(s_minmaxMain, s_condMain, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);
        lv_obj_align_to(s_minmaxShadow, s_minmaxMain, LV_ALIGN_TOP_LEFT, 2, 2);
    }
}

// ---------- Arc callbacks ----------
static void weather_arc_value_changed(lv_event_t* e)
{
    lv_obj_t* arc = lv_event_get_target_obj(e);
    int v = (int)lv_arc_get_value(arc);

    // Clamp and snap to integer segment index 0..4
    if(v < 0) v = 0;
    if(v >= SEG_COUNT) v = SEG_COUNT - 1;

    // Update label from snapped value
    const char* txt = pick_label_for_arc_value(v * SEG_SIZE); // reuse your existing mapping
    if(s_selLabel && txt) lv_label_set_text(s_selLabel, txt);

    // Ensure it stays snapped (prevents intermediate values during drag)
    if(lv_arc_get_value(arc) != v) {
        lv_arc_set_value(arc, v);
        return;
    }

    // Redraw the segmented ring highlight immediately
    lv_obj_invalidate(arc);
}

static void weather_arc_released(lv_event_t* e)
{
    lv_obj_t* arc = lv_event_get_target_obj(e);
int seg = (int)lv_arc_get_value(arc); // 0..4

switch(seg) {
    case 0: // Main
        _ui_screen_change(&ui_MainScreen, LV_SCR_LOAD_ANIM_NONE, 0, 0, ui_MainScreen_screen_init);
        break;
    case 1: // Clock
        _ui_screen_change(&ui_ClockScreen, LV_SCR_LOAD_ANIM_NONE, 0, 0, ui_ClockScreen_screen_init);
        break;
    case 2: // Music
        _ui_screen_change(&ui_MusicControls, LV_SCR_LOAD_ANIM_NONE, 0, 0, ui_MusicControls_screen_init);
        break;
    case 3: // Settings
        _ui_screen_change(&ui_Settings, LV_SCR_LOAD_ANIM_NONE, 0, 0, ui_Settings_screen_init);
        break;
    case 4: // Weather (already here)
    default:
        return;
}
}

// ---------- Background mapping ----------
static const char* pick_bg(uint16_t id, const String& icon)
{
    // OpenWeather icon codes are like "01d", "02n"
    const bool night = (icon.length() >= 3 && icon.charAt(2) == 'n');

    if(id == 800) return night ? "A:/lvgl/weather/clear-night-bg.jpg" : "A:/lvgl/weather/clear-day-bg.jpg";
    if(id == 801) return night ? "A:/lvgl/weather/patchy-night-bg.jpg" : "A:/lvgl/weather/patchy-day-bg.jpg";
    if(id == 802 || id == 803 || id == 804) return "A:/lvgl/weather/cloudy-bg.jpg";

    if(id / 100 == 2) return "A:/lvgl/weather/thunder-bg.jpg";
    if(id / 100 == 3) return "A:/lvgl/weather/drizzle-bg.jpg";

    if(id / 100 == 5) {
        if(id == 500) return "A:/lvgl/weather/light-rain-bg.jpg";
        return "A:/lvgl/weather/rain-bg.jpg";
    }

    if(id / 100 == 6) {
        // 611-616 are sleet-ish in OWM
        if(id >= 611 && id <= 616) return "A:/lvgl/weather/sleet-bg.jpg";
        return "A:/lvgl/weather/snow-bg.jpg";
    }

    if(id / 100 == 7) return "A:/lvgl/weather/fog-bg.jpg";

    // fallback
    return "A:/lvgl/weather/cloudy-bg.jpg";
}

static const char* pick_label_for_arc_value(int v)
{
    if(v < 100) return "Main";
    if(v < 200) return "Clock";
    if(v < 300) return "Music";
    if(v < 400) return "Settings";
    return "Weather";
}

static void set_shadow_label_text(lv_obj_t* shadow, lv_obj_t* main_lbl)
{
    if(!shadow || !main_lbl) return;
    const char* t = lv_label_get_text(main_lbl);
    if(!t) t = "";
    lv_label_set_text(shadow, t);
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

    // Radius to the middle of the stroke
    const int32_t r = (LV_MIN(w, h) / 2) - (RING_W / 2) - 1;

    const int sel = (int)lv_arc_get_value(obj); // 0..4

    // Base segment
    lv_draw_arc_dsc_t base;
    lv_draw_arc_dsc_init(&base);
    base.center.x = (lv_coord_t)cx;
    base.center.y = (lv_coord_t)cy;
    base.radius   = (lv_coord_t)r;
    base.width    = (lv_coord_t)RING_W;
    base.opa      = LV_OPA_COVER;
    base.color    = lv_color_hex(0x0C1118);
    base.rounded  = 0;

    // Highlight segment
    lv_draw_arc_dsc_t hi = base;
    hi.color = lv_color_hex(0x2A9DFF);

    const int32_t seg_span = ARC_SWEEP / SEG_COUNT;   // 60
    const int32_t gap      = SEG_GAP_DEG;             // e.g. 6

    for(int i = 0; i < SEG_COUNT; i++) {
        int32_t start = ARC_ROT + (i * seg_span) + (gap / 2);
        int32_t end   = ARC_ROT + ((i + 1) * seg_span) - (gap / 2);

        base.start_angle = start;
        base.end_angle   = end;
        lv_draw_arc(layer, &base);

        if(i == sel) {
            hi.start_angle = start;
            hi.end_angle   = end;
            lv_draw_arc(layer, &hi);
        }
    }
}