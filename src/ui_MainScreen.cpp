#include "ui.h"
#include <Arduino.h>
#include "clock.h"
#include "ui_MainScreen.h"
#include <lvgl.h>
#include "esp_heap_caps.h" // Include this header for heap_caps_malloc
#include "WeatherManager.h"

#include <time.h>


lv_obj_t * second_arc;
lv_obj_t * minute_arc;
lv_obj_t * hour_arc;

// Mid-ring + arc labels (date + digital time)
static lv_obj_t * ui_MidInfoRing = NULL;
static lv_obj_t * ui_MidInfoBoundary = NULL;
static lv_obj_t * ui_DateArcLabel = NULL;
static lv_obj_t * ui_TimeArcLabel = NULL;

// --- Tide ring / tide graph around mid info ring ---

static constexpr float PI_F = 3.14159265f;

// LVGL objects for tide UI
static lv_obj_t * ui_TideCanvas     = nullptr;  // canvas for segmented ring
static uint8_t  * tideCanvasBuf     = nullptr;  // ARGB8888 buffer in PSRAM

static lv_obj_t * ui_TideLine       = nullptr;  // keep for geometry if needed
static lv_obj_t * ui_TideMarker     = nullptr;  // dot showing "now"
static lv_obj_t * ui_TideStatusLabel = nullptr; // small text status




static lv_obj_t * ui_TideHighLabel  = nullptr;  // "High" label
static lv_obj_t * ui_TideLowLabel   = nullptr;  // "Low" label

// Geometry: tide ring between menu arc (~100) and mid info ring (radius 130)
static constexpr int TIDE_RING_DIAMETER   = 260;  // matches ui_MidInfoRing bbox
static constexpr int TIDE_RING_R_INNER    = 105;  // just outside menu arc
static constexpr int TIDE_RING_R_OUTER    = 125;  // just inside mid info ring

// Sample buffer
static constexpr uint16_t TIDE_MAX_SAMPLES = 128;
static float    tideSamples[TIDE_MAX_SAMPLES];
static uint16_t tideSampleCount    = 0;
static bool     tideCurveValid     = false;
static time_t   tideFirstSampleUtc = 0;   // epoch of heights[0]
static uint32_t tideSampleStepSec  = 0;   // seconds between samples

// LVGL 9 line points (still used for +/- labels + marker positioning)
static lv_point_precise_t tidePoints[TIDE_MAX_SAMPLES];
static uint16_t           tideHighIndex = 0;
static uint16_t           tideLowIndex  = 0;

// Segmented ring parameters
static constexpr uint16_t TIDE_SEG_COUNT   = 24;
static constexpr uint16_t TIDE_SEG_GAP_DEG = 6;

static lv_obj_t * ui_TideBars[TIDE_SEG_COUNT];
static lv_point_precise_t tideBarPoints[TIDE_SEG_COUNT][2];
// Forward declarations
static void tide_compute_minmax(float &outMin, float &outMax);
static void draw_tide_curve();
static void draw_tide_segments(float hMin, float hMax);
static void update_tide_marker();
static void tide_set_status(const char *msg, lv_color_t color);
static lv_color_t lerp_color(lv_color_t c1, lv_color_t c2, float t);

// Public UI API, called from TideService/WeatherManager
void ui_mainscreen_set_tide_curve(const float *heights,
                                  uint16_t     count,
                                  time_t       firstSampleUtc,
                                  uint32_t     stepSeconds);


#define CANVAS_WIDTH  466
#define CANVAS_HEIGHT 466

extern const char* getMeteoconIcon(uint16_t weatherId, bool day);

static int16_t norm_angle_deg(float a)
{
    while (a < 0.0f)   a += 360.0f;
    while (a >= 360.0f) a -= 360.0f;
    return (int16_t)(a + 0.5f);
}

void ui_MainScreen_screen_init(void)
{
    ui_MainScreen = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_MainScreen, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_bg_color(ui_MainScreen, lv_color_hex(0x01070f), LV_PART_MAIN | LV_STATE_DEFAULT);
    //lv_obj_set_style_bg_color(ui_MainScreen, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_MainScreen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(ui_MainScreen, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_main_stop(ui_MainScreen, 100, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_stop(ui_MainScreen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    
      // Initialize the main arc menu with glow effect on the indicator
    ui_MainArcMenu = lv_arc_create(ui_MainScreen);
    lv_obj_set_width(ui_MainArcMenu, 200);
    lv_obj_set_height(ui_MainArcMenu, 200);
    lv_obj_set_align(ui_MainArcMenu, LV_ALIGN_CENTER);
    lv_arc_set_range(ui_MainArcMenu, 0, 500);
    lv_arc_set_value(ui_MainArcMenu, 100);
    lv_obj_set_style_arc_width(ui_MainArcMenu, 10, LV_PART_MAIN);
    
    lv_obj_set_style_arc_color(ui_MainArcMenu, lv_color_hex(0x01070f), LV_PART_MAIN);

    lv_obj_set_style_arc_color(ui_MainArcMenu, lv_color_hex(0x79CBFC), LV_PART_INDICATOR);

    lv_obj_remove_style(ui_MainArcMenu, NULL, LV_PART_KNOB); // Remove knob
     lv_obj_set_style_arc_width(ui_MainArcMenu, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(ui_MainArcMenu, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(ui_MainArcMenu, false, LV_PART_INDICATOR | LV_STATE_DEFAULT);
   
    // Main arc style
    static lv_style_t style_main_arc;
    lv_style_init(&style_main_arc);

    lv_style_set_arc_opa(&style_main_arc, 255);
    lv_style_set_arc_rounded(&style_main_arc, false); // Ensuring no rounded ends for main arc

    // Indicator arc style with a more intense glow effect
    static lv_style_t style_indicator_glow;
    lv_style_init(&style_indicator_glow);
    lv_style_set_arc_color(&style_indicator_glow, lv_color_hex(0x79CBFC));
    lv_style_set_arc_opa(&style_indicator_glow, 255);
    lv_style_set_arc_width(&style_indicator_glow, 10); // Increased width for visibility
    lv_style_set_arc_rounded(&style_indicator_glow, false); // Ensuring no rounded ends for indicator
   // lv_style_set_shadow_color(&style_indicator_glow, lv_color_hex(0x79CBFC));
    //lv_style_set_shadow_opa(&style_indicator_glow, 50);
    //lv_style_set_shadow_width(&style_indicator_glow, 20); // Increased shadow width for stronger glow
    //lv_style_set_shadow_offset_x(&style_indicator_glow, 10);
    //lv_style_set_shadow_offset_y(&style_indicator_glow, 10);
    //lv_style_set_shadow_spread(&style_indicator_glow, 15);
    //lv_style_set_radius(&style_indicator_glow, 100); // No rounding

    // Apply the styles to the arc
    lv_obj_add_style(ui_MainArcMenu, &style_main_arc, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_style(ui_MainArcMenu, &style_indicator_glow, LV_PART_INDICATOR | LV_STATE_DEFAULT | LV_STATE_PRESSED);

    ui_BatteryArc = lv_arc_create(ui_MainScreen);
    lv_obj_set_size(ui_BatteryArc, 170, 170);
    lv_obj_align(ui_BatteryArc, LV_ALIGN_CENTER, 0, 0);
    lv_arc_set_bg_angles(ui_BatteryArc, 0, 360);
    lv_arc_set_rotation(ui_BatteryArc, 270); // Start from top
    lv_arc_set_value(ui_BatteryArc, 100); // Initially 100%
    lv_obj_remove_style(ui_BatteryArc, NULL, LV_PART_KNOB); // Remove knob
    lv_obj_clear_flag(ui_BatteryArc, LV_OBJ_FLAG_CLICKABLE); // Non-clickable

    // Set initial style
    lv_obj_set_style_arc_width(ui_BatteryArc, 5, LV_PART_MAIN);
    lv_obj_set_style_arc_width(ui_BatteryArc, 5, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(ui_BatteryArc, lv_color_hex(0x01070f), LV_PART_MAIN);
    lv_obj_set_style_arc_color(ui_BatteryArc, lv_color_hex(0x00FF00), LV_PART_INDICATOR); // Green color

    ui_BatteryLabel = lv_label_create(ui_MainScreen);
    lv_obj_set_style_text_font(ui_BatteryLabel, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);

        lv_obj_set_y(ui_BatteryLabel, 20);
        lv_obj_set_align(ui_BatteryLabel, LV_ALIGN_CENTER);
        lv_obj_set_style_text_color(ui_BatteryLabel, lv_color_hex(0x41C7FF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(ui_BatteryLabel, "?????");

        ui_WiFiLabel = lv_label_create(ui_MainScreen);
        lv_obj_set_width(ui_WiFiLabel, LV_SIZE_CONTENT);   /// 1
        lv_obj_set_height(ui_WiFiLabel, LV_SIZE_CONTENT);    /// 1
        lv_obj_set_x(ui_WiFiLabel, -35);
        lv_obj_set_y(ui_WiFiLabel, 50);
        lv_obj_set_align(ui_WiFiLabel, LV_ALIGN_CENTER);
        lv_label_set_text(ui_WiFiLabel, LV_SYMBOL_WIFI);
        //lv_obj_set_style_text_color(ui_WiFiLabel, lv_color_hex(0x41C7FF), LV_PART_MAIN | LV_STATE_DEFAULT);
        //Setting the WIFI label "off" colour
        lv_obj_set_style_text_color(ui_WiFiLabel, lv_color_hex(0x005578), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_opa(ui_WiFiLabel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(ui_WiFiLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(ui_WiFiLabel, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);

        ui_BTLabel = lv_label_create(ui_MainScreen);
        lv_obj_set_width(ui_BTLabel, LV_SIZE_CONTENT);   /// 1
        lv_obj_set_height(ui_BTLabel, LV_SIZE_CONTENT);    /// 1
        lv_obj_set_x(ui_BTLabel, 35);
        lv_obj_set_y(ui_BTLabel, 50);
        lv_obj_set_align(ui_BTLabel, LV_ALIGN_CENTER);
        lv_label_set_text(ui_BTLabel, LV_SYMBOL_BLUETOOTH);
        lv_obj_set_style_text_color(ui_BTLabel, lv_color_hex(0x41C7FF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_opa(ui_BTLabel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(ui_BTLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(ui_BTLabel, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);

        ui_screenselectlabel = lv_label_create(ui_MainScreen);
        lv_obj_set_width(ui_screenselectlabel, LV_SIZE_CONTENT);   /// 1
        lv_obj_set_height(ui_screenselectlabel, LV_SIZE_CONTENT);    /// 1
        lv_obj_set_align(ui_screenselectlabel, LV_ALIGN_CENTER);
        lv_label_set_text(ui_screenselectlabel, "Main");
        lv_obj_set_style_text_color(ui_screenselectlabel, lv_color_hex(0xFFAD00), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_opa(ui_screenselectlabel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

        ui_WeatherImage = lv_img_create(ui_MainScreen);
        lv_obj_set_width(ui_WeatherImage, 32);
        lv_obj_set_height(ui_WeatherImage, 32);
        lv_obj_set_x(ui_WeatherImage, -20);
        lv_obj_set_y(ui_WeatherImage, -50);
        lv_obj_set_align(ui_WeatherImage, LV_ALIGN_CENTER);
        lv_obj_add_flag(ui_WeatherImage, LV_OBJ_FLAG_ADV_HITTEST);     /// Flags
        lv_obj_clear_flag(ui_WeatherImage, LV_OBJ_FLAG_SCROLLABLE);      /// Flags

        ui_WeatherLabel = lv_label_create(ui_MainScreen);
        lv_obj_set_width(ui_WeatherLabel, LV_SIZE_CONTENT);   /// 1
        lv_obj_set_height(ui_WeatherLabel, LV_SIZE_CONTENT);    /// 1
        lv_obj_set_x(ui_WeatherLabel, 20);
        lv_obj_set_y(ui_WeatherLabel, -50);
        lv_obj_set_align(ui_WeatherLabel, LV_ALIGN_CENTER);
        lv_label_set_text(ui_WeatherLabel, "?"); // needs to have the symbol °
        lv_obj_set_style_text_color(ui_WeatherLabel, lv_color_hex(0x41C7FF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_opa(ui_WeatherLabel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(ui_WeatherLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(ui_WeatherLabel, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_add_event_cb(ui_MainArcMenu, ui_event_MainArcMenu, LV_EVENT_ALL, NULL);

        // NEW STUFF: CLOCK

        // Create arcs for seconds, minutes, and hours
        second_arc = lv_arc_create(ui_MainScreen);
        lv_obj_set_size(second_arc, 450, 450);
        lv_obj_center(second_arc);
        lv_arc_set_range(second_arc, 0, 60);
        lv_arc_set_rotation(second_arc, 270);
        lv_arc_set_bg_angles(second_arc, 0, 360);
        lv_arc_set_value(second_arc, 0);
        lv_obj_set_style_arc_opa(second_arc, 255, LV_PART_MAIN);
        lv_obj_set_style_arc_width(second_arc, 5, LV_PART_INDICATOR);
        lv_obj_set_style_arc_width(second_arc, 5, LV_PART_MAIN);
        lv_obj_set_style_arc_color(second_arc, lv_color_hex(0x0892fc), LV_PART_INDICATOR);
        lv_obj_remove_style(second_arc, NULL, LV_PART_KNOB); // Remove knob
        lv_obj_set_style_arc_color(second_arc, lv_color_hex(0x01070f), LV_PART_MAIN);
        lv_obj_clear_flag(second_arc, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_arc_rounded(second_arc, false, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_arc_rounded(second_arc, false, LV_PART_INDICATOR | LV_STATE_DEFAULT);

        minute_arc = lv_arc_create(ui_MainScreen);
        lv_obj_set_size(minute_arc, 430, 430);
        lv_obj_center(minute_arc);
        lv_arc_set_range(minute_arc, 0, 60);
        lv_arc_set_rotation(minute_arc, 270);
        lv_arc_set_bg_angles(minute_arc, 0, 360);
        lv_arc_set_value(minute_arc, 0);
        lv_obj_set_style_arc_opa(minute_arc, 255, LV_PART_MAIN);
        lv_obj_set_style_arc_width(minute_arc, 5, LV_PART_INDICATOR);
        lv_obj_set_style_arc_width(minute_arc, 5, LV_PART_MAIN);
        lv_obj_set_style_arc_color(minute_arc, lv_color_hex(0x8800ff), LV_PART_INDICATOR);
        lv_obj_remove_style(minute_arc, NULL, LV_PART_KNOB); // Remove knob
        lv_obj_set_style_arc_color(minute_arc, lv_color_hex(0x01070f), LV_PART_MAIN);
        lv_obj_clear_flag(minute_arc, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_arc_rounded(minute_arc, false, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_arc_rounded(minute_arc, false, LV_PART_INDICATOR | LV_STATE_DEFAULT);

        hour_arc = lv_arc_create(ui_MainScreen);
        lv_obj_set_size(hour_arc, 410, 410);
        lv_obj_center(hour_arc);
        lv_arc_set_range(hour_arc, 0, 12);
        lv_arc_set_rotation(hour_arc, 270);
        lv_arc_set_bg_angles(hour_arc, 0, 360);
        lv_arc_set_value(hour_arc, 0);
        lv_obj_set_style_arc_opa(hour_arc, 255, LV_PART_MAIN);
        lv_obj_set_style_arc_width(hour_arc, 5, LV_PART_INDICATOR);
        lv_obj_set_style_arc_width(hour_arc, 5, LV_PART_MAIN);
        lv_obj_set_style_arc_color(hour_arc, lv_color_hex(0xcb2eff), LV_PART_INDICATOR);
        lv_obj_remove_style(hour_arc, NULL, LV_PART_KNOB); // Remove knob
        lv_obj_set_style_arc_color(hour_arc, lv_color_hex(0x01070f), LV_PART_MAIN);
        lv_obj_clear_flag(hour_arc, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_arc_rounded(hour_arc, false, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_arc_rounded(hour_arc, false, LV_PART_INDICATOR | LV_STATE_DEFAULT);

        // --- Mid info ring (between outer clock scale and inner arcs) ---
        // Visual: a single subtle/dark blue circle.
        // Content: date around ~1-2 o'clock, digital time around ~10 o'clock.
        ui_MidInfoRing = lv_arc_create(ui_MainScreen);
        lv_obj_set_size(ui_MidInfoRing, 260, 260);
        lv_obj_center(ui_MidInfoRing);
        lv_arc_set_bg_angles(ui_MidInfoRing, 0, 360);
        lv_arc_set_rotation(ui_MidInfoRing, 270);
        lv_arc_set_value(ui_MidInfoRing, 0);
        lv_obj_remove_style(ui_MidInfoRing, NULL, LV_PART_KNOB);
        lv_obj_clear_flag(ui_MidInfoRing, LV_OBJ_FLAG_CLICKABLE);

        // Ring line style
        lv_obj_set_style_arc_width(ui_MidInfoRing, 2, LV_PART_MAIN);
        lv_obj_set_style_arc_color(ui_MidInfoRing, lv_color_hex(0x103357), LV_PART_MAIN); // dark-ish blue
        lv_obj_set_style_arc_opa(ui_MidInfoRing, 200, LV_PART_MAIN);

        // Hide indicator completely
        lv_obj_set_style_arc_width(ui_MidInfoRing, 0, LV_PART_INDICATOR);
        lv_obj_set_style_arc_opa(ui_MidInfoRing, 0, LV_PART_INDICATOR);


          ui_MidInfoBoundary = lv_arc_create(ui_MainScreen);
        lv_obj_set_size(ui_MidInfoBoundary, 300, 300);
        lv_obj_center(ui_MidInfoBoundary);
        lv_arc_set_bg_angles(ui_MidInfoBoundary, 0, 360);
        lv_arc_set_rotation(ui_MidInfoBoundary, 270);
        lv_arc_set_value(ui_MidInfoBoundary, 0);
        lv_obj_remove_style(ui_MidInfoBoundary, NULL, LV_PART_KNOB);
        lv_obj_clear_flag(ui_MidInfoBoundary, LV_OBJ_FLAG_CLICKABLE);

        // Ring line style
        lv_obj_set_style_arc_width(ui_MidInfoBoundary, 2, LV_PART_MAIN);
        lv_obj_set_style_arc_color(ui_MidInfoBoundary, lv_color_hex(0x103357), LV_PART_MAIN); // dark-ish blue
        lv_obj_set_style_arc_opa(ui_MidInfoBoundary, 200, LV_PART_MAIN);

        // Hide indicator completely
        lv_obj_set_style_arc_width(ui_MidInfoBoundary, 0, LV_PART_INDICATOR);
        lv_obj_set_style_arc_opa(ui_MidInfoBoundary, 0, LV_PART_INDICATOR);

    

     // --- Tide segments: 24 arc "cells" around the ring ---
    // We use 24 lv_arc objects, one per segment, with their own angle range.
    {
        const float angleStep   = 360.0f / (float)TIDE_SEG_COUNT;   // 15° per cell
        const float gapDeg      = (float)TIDE_SEG_GAP_DEG;          // e.g. 6°
        const float spanDeg     = angleStep - gapDeg;               // visible sweep
        const float baseAngle   = 270.0f;                           // 12 o'clock

        const lv_color_t noDataColor = lv_color_hex(0x103357);      // dark blue

        for (uint16_t seg = 0; seg < TIDE_SEG_COUNT; ++seg) {
            lv_obj_t *arc = lv_arc_create(ui_MainScreen);
            ui_TideBars[seg] = arc;   // reusing this array for arc objects

            // Start with a nominal size; we'll tweak size/width in draw_tide_segments().
            lv_obj_set_size(arc, TIDE_RING_DIAMETER, TIDE_RING_DIAMETER);
            lv_obj_center(arc);
            lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_flag(arc, LV_OBJ_FLAG_IGNORE_LAYOUT);
            lv_obj_remove_style(arc, NULL, LV_PART_KNOB); // no knob

            // Indicator is unused – hide it completely
            lv_obj_set_style_arc_width(arc, 0, LV_PART_INDICATOR);
            lv_obj_set_style_arc_opa(arc, 0, LV_PART_INDICATOR);

            // Compute this segment's angle window (like the arc menu, but sliced)
            float centreDeg = baseAngle + (float)seg * angleStep;
            float startDeg  = centreDeg - spanDeg * 0.5f;
            float endDeg    = centreDeg + spanDeg * 0.5f;

            int16_t start_i = norm_angle_deg(startDeg);
            int16_t end_i   = norm_angle_deg(endDeg);

            lv_arc_set_bg_angles(arc, start_i, end_i);
            lv_arc_set_rotation(arc, 0);  // we baked the offset into start/end

            // "No data yet" style: mid-thickness, dark blue
            lv_obj_set_style_arc_width(arc, 8, LV_PART_MAIN);
            lv_obj_set_style_arc_color(arc, noDataColor, LV_PART_MAIN);
            lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_MAIN);

            // We *do not* hide them; they form the empty "cells" you wanted.
        }
    }

  

    // --- Tide ring line (legacy, kept for geometry only; not drawn) ---
    ui_TideLine = lv_line_create(ui_MainScreen);
    lv_obj_set_size(ui_TideLine, TIDE_RING_DIAMETER, TIDE_RING_DIAMETER);
    lv_obj_center(ui_TideLine);
    lv_obj_clear_flag(ui_TideLine, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(ui_TideLine, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_style_line_width(ui_TideLine, 2, 0);
    lv_obj_set_style_line_color(ui_TideLine, lv_color_hex(0x0889BF), 0);
    lv_obj_set_style_line_rounded(ui_TideLine, true, 0);
    lv_obj_add_flag(ui_TideLine, LV_OBJ_FLAG_HIDDEN);  // never shown now




    // "Now" marker dot for current tide level
    ui_TideMarker = lv_obj_create(ui_MainScreen);
    lv_obj_set_size(ui_TideMarker, 10, 10);
    lv_obj_set_style_radius(ui_TideMarker, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui_TideMarker, lv_color_hex(0x41C7FF), LV_PART_MAIN); // brighter cyan
    lv_obj_set_style_bg_opa(ui_TideMarker, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_opa(ui_TideMarker, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(ui_TideMarker, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(ui_TideMarker, LV_OBJ_FLAG_HIDDEN);


    
    // NEW: high tide label ("+")
    ui_TideHighLabel = lv_label_create(ui_MainScreen);
    lv_label_set_text(ui_TideHighLabel, "+");
    lv_obj_set_style_text_font(ui_TideHighLabel, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(ui_TideHighLabel, lv_color_hex(0x41C7FF), LV_PART_MAIN);
    lv_obj_set_style_text_opa(ui_TideHighLabel, 255, LV_PART_MAIN);
    lv_obj_add_flag(ui_TideHighLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_TideHighLabel, LV_OBJ_FLAG_CLICKABLE);

    // NEW: low tide label ("-")
    ui_TideLowLabel = lv_label_create(ui_MainScreen);
    lv_label_set_text(ui_TideLowLabel, "-");
    lv_obj_set_style_text_font(ui_TideLowLabel, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(ui_TideLowLabel, lv_color_hex(0x41C7FF), LV_PART_MAIN);
    lv_obj_set_style_text_opa(ui_TideLowLabel, 255, LV_PART_MAIN);
    lv_obj_add_flag(ui_TideLowLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_TideLowLabel, LV_OBJ_FLAG_CLICKABLE);




    // Tide status label – shows "Tide: --" or errors if we screw up
    ui_TideStatusLabel = lv_label_create(ui_MainScreen);
    lv_obj_set_width(ui_TideStatusLabel, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_TideStatusLabel, LV_SIZE_CONTENT);
    lv_obj_align(ui_TideStatusLabel, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_label_set_text(ui_TideStatusLabel, "Tide: --");
    lv_obj_set_style_text_font(ui_TideStatusLabel, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(ui_TideStatusLabel, lv_color_hex(0x555555), LV_PART_MAIN);
    lv_obj_set_style_text_opa(ui_TideStatusLabel, 200, LV_PART_MAIN);







        // --- Date arc label ---
        ui_DateArcLabel = lv_arclabel_create(ui_MainScreen);
        lv_obj_set_size(ui_DateArcLabel, 300, 300);
        lv_obj_center(ui_DateArcLabel);
        lv_obj_clear_flag(ui_DateArcLabel, LV_OBJ_FLAG_CLICKABLE);
        lv_arclabel_set_radius(ui_DateArcLabel, 130);
        lv_arclabel_set_angle_start(ui_DateArcLabel, 290); // ~1 o'clock
        lv_arclabel_set_angle_size(ui_DateArcLabel, 150);
        lv_arclabel_set_dir(ui_DateArcLabel, LV_ARCLABEL_DIR_CLOCKWISE);
        lv_arclabel_set_text_vertical_align(ui_DateArcLabel, LV_ARCLABEL_TEXT_ALIGN_TRAILING);
        lv_arclabel_set_text_horizontal_align(ui_DateArcLabel, LV_ARCLABEL_TEXT_ALIGN_LEADING);
        lv_obj_set_style_text_font(ui_DateArcLabel, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(ui_DateArcLabel, lv_color_hex(0x2A5D8F), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_opa(ui_DateArcLabel, 220, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_arclabel_set_text(ui_DateArcLabel, "--- -- -- ----");

        // --- Digital time arc label ---
        ui_TimeArcLabel = lv_arclabel_create(ui_MainScreen);
        lv_obj_set_size(ui_TimeArcLabel, 300, 300);
        lv_obj_center(ui_TimeArcLabel);
        lv_obj_clear_flag(ui_TimeArcLabel, LV_OBJ_FLAG_CLICKABLE);
        lv_arclabel_set_radius(ui_TimeArcLabel, 130);
        lv_arclabel_set_angle_start(ui_TimeArcLabel, 150); // ~10 o'clock-ish
        lv_arclabel_set_angle_size(ui_TimeArcLabel, 120);
        lv_arclabel_set_dir(ui_TimeArcLabel, LV_ARCLABEL_DIR_CLOCKWISE);
        lv_arclabel_set_text_vertical_align(ui_TimeArcLabel, LV_ARCLABEL_TEXT_ALIGN_TRAILING);
        lv_arclabel_set_text_horizontal_align(ui_TimeArcLabel, LV_ARCLABEL_TEXT_ALIGN_CENTER);
        lv_obj_set_style_text_font(ui_TimeArcLabel, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(ui_TimeArcLabel, lv_color_hex(0x2A5D8F), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_opa(ui_TimeArcLabel, 220, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_arclabel_set_text(ui_TimeArcLabel, "--:--:--");

      


        create_combined_scale();
        
        // Call update to set initial values


        update_main_screen();
    
}

void create_segmented_ring(lv_obj_t * parent) {
    /* Allocate buffer in PSRAM */
    //lv_color_t * canvas_buf = (lv_color_t *)heap_caps_malloc(CANVAS_WIDTH * CANVAS_HEIGHT * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    static uint8_t * canvas_buf = (uint8_t *)heap_caps_malloc(CANVAS_WIDTH * CANVAS_HEIGHT * sizeof(LV_COLOR_FORMAT_ARGB8888), MALLOC_CAP_SPIRAM);
    
    if(canvas_buf == NULL) {
        // Handle allocation failure
        printf("Failed to allocate canvas buffer in PSRAM\n");
        return;
    }
 /* Create the canvas object */
    lv_obj_t * canvas = lv_canvas_create(parent);

    /* Set the drawing buffer for the canvas */
   // lv_canvas_set_draw_buf(canvas, canvas_buf);
   lv_canvas_set_buffer(canvas, canvas_buf, CANVAS_WIDTH, CANVAS_HEIGHT, LV_COLOR_FORMAT_ARGB8888);

    /* Set the canvas size */
    lv_obj_set_size(canvas, CANVAS_WIDTH, CANVAS_HEIGHT);

    /* Fill the canvas background (optional) */
  //  lv_canvas_fill_bg(canvas, lv_color_hex3(0x000), 255);

    /* Center the canvas */
    lv_obj_center(canvas);

    /* Initialize the drawing layer */
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);

    /* Define segment parameters */
    uint16_t num_segments = 12; // Number of segments
    uint16_t gap_degree = 15;    // Gap between segments in degrees

    /* Calculate the angle covered by each segment */
    uint16_t total_gap = gap_degree * num_segments;
    uint16_t segment_angle = (360 - total_gap) / num_segments;

    /* Arc descriptor */
    lv_draw_arc_dsc_t arc_dsc;
    lv_draw_arc_dsc_init(&arc_dsc);
    //arc_dsc.color = lv_color_hex(0x41C7FF); // Pale cyan color
    
    arc_dsc.color = lv_color_hex(0x0889bf); //darker cyan color
    arc_dsc.width = 10;                     // Thickness of segments
    arc_dsc.rounded = 0;                    // Set to 1 for rounded ends
    arc_dsc.opa = LV_OPA_COVER;             // Opacity

    /* Set center and radius */
    arc_dsc.center.x = CANVAS_WIDTH / 2;
    arc_dsc.center.y = CANVAS_HEIGHT / 2;
    arc_dsc.radius = (CANVAS_WIDTH < CANVAS_HEIGHT ? CANVAS_WIDTH : CANVAS_HEIGHT) / 2 - arc_dsc.width / 2;

    /* Start drawing each segment */
    uint16_t start_angle = 0;
    for(uint16_t i = 0; i < num_segments; i++)
    {
        uint16_t end_angle = start_angle + segment_angle;

        /* Set the start and end angles (in 0.1 degrees) */
        arc_dsc.start_angle = start_angle;
        arc_dsc.end_angle = end_angle;

        /* Draw the arc (segment) */
        lv_draw_arc(&layer, &arc_dsc);

        /* Increment start angle by segment angle and gap */
        start_angle = end_angle + gap_degree;
    }

    /* Finish drawing on the layer */
    lv_canvas_finish_layer(canvas, &layer);

    // No need to free canvas_buf here; it will be freed when the canvas is deleted

}

void create_combined_scale(void) {
    lv_obj_t * MainClockScale = lv_scale_create(ui_MainScreen);
    lv_obj_clear_flag(MainClockScale, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_blend_mode(MainClockScale, LV_BLEND_MODE_MULTIPLY, LV_PART_ANY);
    // Set the size to cover the outer ring
    lv_obj_set_size(MainClockScale, 466, 466);
    lv_obj_center(MainClockScale);

    // Configure the scale
    lv_scale_set_mode(MainClockScale, LV_SCALE_MODE_ROUND_INNER);
    lv_scale_set_rotation(MainClockScale, 270); // Start at the top
    lv_scale_set_angle_range(MainClockScale, 360);

    // Set the number of ticks
    lv_scale_set_total_tick_count(MainClockScale, 60 + 1); // +1 to include the last tick

    // Set major tick every 5 ticks (12 major ticks for hours)
    lv_scale_set_major_tick_every(MainClockScale, 5);

    // Hide labels
    lv_scale_set_label_show(MainClockScale, false);

     lv_obj_set_style_arc_width(MainClockScale, 2, LV_PART_MAIN);
    lv_obj_set_style_arc_color(MainClockScale, lv_color_hex(0x41C7FF), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(MainClockScale, 100, LV_PART_MAIN);

    // Set the radius to match your outermost arc
   // lv_obj_set_style_radius(MainClockScale, SCALE_RADIUS, 0);

     // Style for minor ticks (minute/second markers)
    lv_obj_set_style_line_width(MainClockScale, 1, LV_PART_ITEMS); // Thickness
    //lv_obj_set_style_scale_end_line_width(MainClockScale, 1, LV_PART_ITEMS); // For symmetry
    lv_obj_set_style_length(MainClockScale, 10, LV_PART_ITEMS); // Length
   // lv_obj_set_style_line_color(MainClockScale, lv_color_hex(0x19515e), LV_PART_ITEMS); // Color
    lv_obj_set_style_line_color(MainClockScale, lv_color_hex(0x41C7FF), LV_PART_ITEMS); // Color
    lv_obj_set_style_arc_opa(MainClockScale, 100, LV_PART_ITEMS);

    // Style for major ticks (hour markers)
    lv_obj_set_style_line_width(MainClockScale, 5, LV_PART_INDICATOR); // Thickness
    //lv_obj_set_style_scale_end_line_width(MainClockScale, 3, LV_PART_TICKS); // For symmetry
    lv_obj_set_style_length(MainClockScale, 10, LV_PART_INDICATOR); // Length
    lv_obj_set_style_line_color(MainClockScale, lv_color_hex(0x41C7FF), LV_PART_INDICATOR); // Color

    // Adjust colors to match your arcs if needed
    // For example, use lv_color_hex(0x41C7FF) or other colors


}

// --- Tide helpers -----------------------------------------------------------

static lv_color_t lerp_color(lv_color_t c1, lv_color_t c2, float t)
{
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    uint32_t u1 = lv_color_to_u32(c1);
    uint32_t u2 = lv_color_to_u32(c2);

    // 0xAARRGGBB
    uint8_t r1 = (u1 >> 16) & 0xFF;
    uint8_t g1 = (u1 >> 8)  & 0xFF;
    uint8_t b1 =  u1        & 0xFF;

    uint8_t r2 = (u2 >> 16) & 0xFF;
    uint8_t g2 = (u2 >> 8)  & 0xFF;
    uint8_t b2 =  u2        & 0xFF;

    uint8_t r = (uint8_t)(r1 + (int16_t)(r2 - r1) * t);
    uint8_t g = (uint8_t)(g1 + (int16_t)(g2 - g1) * t);
    uint8_t b = (uint8_t)(b1 + (int16_t)(b2 - b1) * t);

    return lv_color_make(r, g, b); // NOTE: lv_color_make(r,g,b) in your build; adjust if needed
}


static void tide_set_status(const char *msg, lv_color_t color)
{
    if (!ui_TideStatusLabel) return;
    lv_label_set_text(ui_TideStatusLabel, msg ? msg : "Tide: --");
    lv_obj_set_style_text_color(ui_TideStatusLabel, color, LV_PART_MAIN);
}

static void tide_compute_minmax(float &outMin, float &outMax)
{
    if (!tideCurveValid || tideSampleCount == 0) {
        outMin = 0.0f;
        outMax = 1.0f;
        return;
    }

    float mn = tideSamples[0];
    float mx = tideSamples[0];
    for (uint16_t i = 1; i < tideSampleCount; ++i) {
        float v = tideSamples[i];
        if (v < mn) mn = v;
        if (v > mx) mx = v;
    }

    // Avoid zero-span
    if (mx - mn < 0.01f) {
        mx = mn + 0.01f;
    }

    outMin = mn;
    outMax = mx;
}

static void update_tide_extreme_labels()
{
    if (!ui_TideLine ||
        !ui_TideHighLabel || !ui_TideLowLabel ||
        !tideCurveValid || tideSampleCount < 2) {

        if (ui_TideHighLabel) lv_obj_add_flag(ui_TideHighLabel, LV_OBJ_FLAG_HIDDEN);
        if (ui_TideLowLabel)  lv_obj_add_flag(ui_TideLowLabel,  LV_OBJ_FLAG_HIDDEN);
        return;
    }

    float hMin, hMax;
    tide_compute_minmax(hMin, hMax);
    const float span = (hMax - hMin);
    if (span <= 0.0f) {
        lv_obj_add_flag(ui_TideHighLabel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_TideLowLabel,  LV_OBJ_FLAG_HIDDEN);
        return;
    }

    // Find indices of min and max
    uint16_t minIdx = 0;
    uint16_t maxIdx = 0;
    for (uint16_t i = 1; i < tideSampleCount; ++i) {
        if (tideSamples[i] < tideSamples[minIdx]) minIdx = i;
        if (tideSamples[i] > tideSamples[maxIdx]) maxIdx = i;
    }

    const lv_coord_t line_x = lv_obj_get_x(ui_TideLine);
    const lv_coord_t line_y = lv_obj_get_y(ui_TideLine);
    const float cx = line_x + (TIDE_RING_DIAMETER / 2.0f);
    const float cy = line_y + (TIDE_RING_DIAMETER / 2.0f);

    // --- High tide label (outside the ring) ---
    {
        const float angleDeg = 360.0f * (float)maxIdx / (float)tideSampleCount - 90.0f;
        const float rad      = angleDeg * (PI_F / 180.0f);

        const float rHigh = TIDE_RING_R_OUTER + 8.0f; // slightly outside ring

        const float px = cx + cosf(rad) * rHigh;
        const float py = cy + sinf(rad) * rHigh;

        const lv_coord_t w = lv_obj_get_width(ui_TideHighLabel);
        const lv_coord_t h = lv_obj_get_height(ui_TideHighLabel);

        lv_obj_clear_flag(ui_TideHighLabel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(ui_TideHighLabel,
                       (lv_coord_t)px - w / 2,
                       (lv_coord_t)py - h / 2);
    }

    // --- Low tide label (inside the ring) ---
    {
        const float angleDeg = 360.0f * (float)minIdx / (float)tideSampleCount - 90.0f;
        const float rad      = angleDeg * (PI_F / 180.0f);

        const float rLow = TIDE_RING_R_INNER - 8.0f; // slightly inside ring

        const float px = cx + cosf(rad) * rLow;
        const float py = cy + sinf(rad) * rLow;

        const lv_coord_t w = lv_obj_get_width(ui_TideLowLabel);
        const lv_coord_t h = lv_obj_get_height(ui_TideLowLabel);

        lv_obj_clear_flag(ui_TideLowLabel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(ui_TideLowLabel,
                       (lv_coord_t)px - w / 2,
                       (lv_coord_t)py - h / 2);
    }
}

static void draw_tide_segments(float hMin, float hMax)
{
    const lv_color_t noDataColor = lv_color_hex(0x16212b); // dark blue
    const lv_color_t lowColor    = lv_color_hex(0x00a68c); // deep teal
    const lv_color_t highColor   = lv_color_hex(0x42abd6); // bright cyan

    const float baseR = (float)TIDE_RING_R_INNER;                       // inner edge
    const float band  = (float)(TIDE_RING_R_OUTER - TIDE_RING_R_INNER); // max thickness

    const float minLen = 4.0f;   // minimum radial “height”
    const float maxLen = band;   // maximum = outer edge of band

    // If we don't have data, show equal "no data" cells
    if (!tideCurveValid || tideSampleCount < 2 || tideSampleStepSec == 0) {
        const float noDataLen = band * 0.5f;

        for (uint16_t seg = 0; seg < TIDE_SEG_COUNT; ++seg) {
            lv_obj_t *arc = ui_TideBars[seg];
            if (!arc) continue;

            float innerR = baseR;
            float outerR = baseR + noDataLen;
            float radius = 0.5f * (innerR + outerR);
            float width  = outerR - innerR;
            if (width < 1.0f) width = 1.0f;

            lv_coord_t arcWidth = (lv_coord_t)(width + 0.5f);
            lv_coord_t sz       = (lv_coord_t)(2.0f * radius + width + 0.5f);

            lv_obj_set_size(arc, sz, sz);
            lv_obj_center(arc);
            lv_obj_set_style_arc_width(arc, arcWidth, LV_PART_MAIN);
            lv_obj_set_style_arc_color(arc, noDataColor, LV_PART_MAIN);
            lv_obj_clear_flag(arc, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    float span = hMax - hMin;
    if (span < 0.01f) span = 0.01f;

    // Accumulate average height per 30-minute clock slot (24 segments over 12h)
    float    segSum[TIDE_SEG_COUNT]   = {0.0f};
    uint16_t segCount[TIDE_SEG_COUNT] = {0};

    for (uint16_t i = 0; i < tideSampleCount; ++i) {
        time_t sampleTime = tideFirstSampleUtc +
                            (time_t)i * (time_t)tideSampleStepSec;

        struct tm lt;
        localtime_r(&sampleTime, &lt);

        int   hour12   = lt.tm_hour % 12;  // 0–11
        float hourFrac = (float)hour12 +
                         (float)lt.tm_min  / 60.0f +
                         (float)lt.tm_sec  / 3600.0f;

        // Position around a 12-hour dial -> 0..1
        float pos  = hourFrac / 12.0f;
        float segF = pos * (float)TIDE_SEG_COUNT;   // 0..24
        int   seg  = (int)segF;                     // floor

        if (seg < 0) seg = 0;
        if (seg >= (int)TIDE_SEG_COUNT) seg = TIDE_SEG_COUNT - 1;

        segSum[seg]   += tideSamples[i];
        segCount[seg] += 1;
    }

    // Now turn each segment's average height into arc thickness + colour
    for (uint16_t seg = 0; seg < TIDE_SEG_COUNT; ++seg) {
        lv_obj_t *arc = ui_TideBars[seg];
        if (!arc) continue;

        float len;
        lv_color_t c;

        if (segCount[seg] == 0) {
            // No samples for this slot – fall back to “no data” look
            const float noDataLen = band * 0.5f;
            len = noDataLen;
            c   = noDataColor;
        } else {
            float h    = segSum[seg] / (float)segCount[seg];
            float norm = (h - hMin) / span;   // 0..1
            if (norm < 0.0f) norm = 0.0f;
            if (norm > 1.0f) norm = 1.0f;

            len = minLen + norm * (maxLen - minLen);
            c   = lerp_color(lowColor, highColor, norm);
        }

        float innerR = baseR;
        float outerR = baseR + len;
        float radius = 0.5f * (innerR + outerR);
        float width  = outerR - innerR;
        if (width < 1.0f) width = 1.0f;

        lv_coord_t arcWidth = (lv_coord_t)(width + 0.5f);
        lv_coord_t sz       = (lv_coord_t)(2.0f * radius + width + 0.5f);

        lv_obj_set_size(arc, sz, sz);
        lv_obj_center(arc);
        lv_obj_set_style_arc_width(arc, arcWidth, LV_PART_MAIN);
        lv_obj_set_style_arc_color(arc, c, LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(arc, false, LV_PART_MAIN);
        lv_obj_clear_flag(arc, LV_OBJ_FLAG_HIDDEN);
    }
}




static void draw_tide_curve()
{
    if (!ui_TideLine || !tideCurveValid || tideSampleCount < 2) {
        if (ui_TideLine)      lv_obj_add_flag(ui_TideLine, LV_OBJ_FLAG_HIDDEN);
        if (ui_TideCanvas)    lv_obj_add_flag(ui_TideCanvas, LV_OBJ_FLAG_HIDDEN);
        if (ui_TideMarker)    lv_obj_add_flag(ui_TideMarker, LV_OBJ_FLAG_HIDDEN);
        if (ui_TideHighLabel) lv_obj_add_flag(ui_TideHighLabel, LV_OBJ_FLAG_HIDDEN);
        if (ui_TideLowLabel)  lv_obj_add_flag(ui_TideLowLabel,  LV_OBJ_FLAG_HIDDEN);

        tide_set_status("Tide: no data", lv_color_hex(0xAA4444));
        return;
    }

    // Compute min/max and indices
    float hMin = tideSamples[0];
    float hMax = tideSamples[0];
    tideLowIndex  = 0;
    tideHighIndex = 0;

    for (uint16_t i = 1; i < tideSampleCount; ++i) {
        float v = tideSamples[i];
        if (v < hMin) {
            hMin = v;
            tideLowIndex = i;
        }
        if (v > hMax) {
            hMax = v;
            tideHighIndex = i;
        }
    }

    float span = hMax - hMin;
    if (span < 0.01f) span = 0.01f;

    // Draw segmented tide ring on canvas
    draw_tide_segments(hMin, hMax);

 // Rebuild tidePoints[] purely for +/- labels and marker, using time-of-day
    const int cx = TIDE_RING_DIAMETER / 2;
    const int cy = TIDE_RING_DIAMETER / 2;

    for (uint16_t i = 0; i < tideSampleCount; ++i) {
        const float h    = tideSamples[i];
        const float norm = (h - hMin) / span; // 0..1

        const float r = (float)TIDE_RING_R_INNER +
                        norm * (float)(TIDE_RING_R_OUTER - TIDE_RING_R_INNER);

        time_t sampleTime = tideFirstSampleUtc +
                            (time_t)i * (time_t)tideSampleStepSec;

        struct tm lt;
        localtime_r(&sampleTime, &lt);

        int   hour12   = lt.tm_hour % 12;
        float hourFrac = (float)hour12 +
                         (float)lt.tm_min / 60.0f +
                         (float)lt.tm_sec / 3600.0f;

        float angleDeg = 360.0f * (hourFrac / 12.0f) - 90.0f;
        float rad      = angleDeg * (PI_F / 180.0f);

        const float x = (float)cx + cosf(rad) * r;
        const float y = (float)cy + sinf(rad) * r;

        tidePoints[i].x = (lv_coord_t)x;
        tidePoints[i].y = (lv_coord_t)y;
    }
    // DO NOT draw the line any more; ui_TideLine stays hidden
    // lv_line_set_points(ui_TideLine, tidePoints, tideSampleCount);
    // lv_obj_clear_flag(ui_TideLine, LV_OBJ_FLAG_HIDDEN);

    // +/- label positioning (unchanged)
    if (ui_TideHighLabel || ui_TideLowLabel) {
        const lv_coord_t line_x = lv_obj_get_x(ui_TideLine);
        const lv_coord_t line_y = lv_obj_get_y(ui_TideLine);

        if (ui_TideHighLabel) {
            lv_coord_t hx = line_x + tidePoints[tideHighIndex].x;
            lv_coord_t hy = line_y + tidePoints[tideHighIndex].y;

            lv_coord_t hw = lv_obj_get_width(ui_TideHighLabel);
            lv_coord_t hh = lv_obj_get_height(ui_TideHighLabel);

            lv_obj_set_pos(ui_TideHighLabel,
                           hx - hw / 2 + 5,
                           hy - hh / 2 + 5);
            lv_obj_clear_flag(ui_TideHighLabel, LV_OBJ_FLAG_HIDDEN);
        }

        if (ui_TideLowLabel) {
            lv_coord_t lx = line_x + tidePoints[tideLowIndex].x;
            lv_coord_t ly = line_y + tidePoints[tideLowIndex].y;

            lv_coord_t lw = lv_obj_get_width(ui_TideLowLabel);
            lv_coord_t lh = lv_obj_get_height(ui_TideLowLabel);

            lv_obj_set_pos(ui_TideLowLabel,
                           lx - lw / 2,
                           ly - lh / 2);
            lv_obj_clear_flag(ui_TideLowLabel, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // Show the marker (position handled in update_tide_marker)
    if (ui_TideMarker)
        lv_obj_clear_flag(ui_TideMarker, LV_OBJ_FLAG_HIDDEN);

    tide_set_status("Tide: OK", lv_color_hex(0x41C7FF));
}


static void update_tide_marker()
{
    if (!ui_TideMarker || !ui_TideLine ||
        !tideCurveValid || tideSampleCount < 2 ||
        tideSampleStepSec == 0) {
        if (ui_TideMarker) lv_obj_add_flag(ui_TideMarker, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    time_t nowUtc = time(nullptr);
    
    
     // "Now" must be within the sampled window [first, last]
    const time_t firstSampleUtc = tideFirstSampleUtc;
    const time_t lastSampleUtc  =
        tideFirstSampleUtc +
        (time_t)(tideSampleCount - 1) * (time_t)tideSampleStepSec;

    if (nowUtc < firstSampleUtc || nowUtc > lastSampleUtc) {
        // Outside sample range: hide marker instead of faking it
        lv_obj_add_flag(ui_TideMarker, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    uint32_t delta = (uint32_t)(nowUtc - firstSampleUtc);
    uint32_t idx   = delta / tideSampleStepSec;
    if (idx >= tideSampleCount) idx = tideSampleCount - 1;
    const uint16_t nowIndex = (uint16_t)idx;

    float hMin, hMax;
    tide_compute_minmax(hMin, hMax);
    const float span = (hMax - hMin);

    const float h    = tideSamples[nowIndex];
    const float norm = (h - hMin) / span;
    const float r    = TIDE_RING_R_INNER +
                       norm * (TIDE_RING_R_OUTER - TIDE_RING_R_INNER);

    // Angle: current *time of day* on a 12-hour dial
    struct tm lt;
    localtime_r(&nowUtc, &lt);

    int   hour12   = lt.tm_hour % 12;
    float hourFrac = (float)hour12 +
                     (float)lt.tm_min / 60.0f +
                     (float)lt.tm_sec / 3600.0f;

    const float angleDeg = 360.0f * (hourFrac / 12.0f) - 90.0f;
    const float rad      = angleDeg * (PI_F / 180.0f);

    // Line object is TIDE_RING_DIAMETER x TIDE_RING_DIAMETER and centered
    const lv_coord_t line_x = lv_obj_get_x(ui_TideLine);
    const lv_coord_t line_y = lv_obj_get_y(ui_TideLine);
    const float cx = line_x + (TIDE_RING_DIAMETER / 2.0f);
    const float cy = line_y + (TIDE_RING_DIAMETER / 2.0f);

    const float px = cx + cosf(rad) * r;
    const float py = cy + sinf(rad) * r;

    const lv_coord_t marker_w = lv_obj_get_width(ui_TideMarker);
    const lv_coord_t marker_h = lv_obj_get_height(ui_TideMarker);

    lv_obj_clear_flag(ui_TideMarker, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(ui_TideMarker,
                   (lv_coord_t)px - marker_w / 2,
                   (lv_coord_t)py - marker_h / 2);
}

/**
 * Called from TideService / WeatherManager after you've built a regular time grid
 * of tide heights.
 *
 *  - heights: array of tide heights in metres
 *  - count:   number of samples (>= 2, <= TIDE_MAX_SAMPLES)
 *  - firstSampleUtc: epoch time of heights[0]
 *  - stepSeconds:    spacing between samples in seconds
 */
void ui_mainscreen_set_tide_curve(const float *heights,
                                  uint16_t     count,
                                  time_t       firstSampleUtc,
                                  uint32_t     stepSeconds)
{
    if (!heights || count < 2 || count > TIDE_MAX_SAMPLES || stepSeconds == 0) {
        tideCurveValid     = false;
        tideSampleCount    = 0;
        tideFirstSampleUtc = 0;
        tideSampleStepSec  = 0;

        if (ui_TideLine)   lv_obj_add_flag(ui_TideLine, LV_OBJ_FLAG_HIDDEN);
        if (ui_TideMarker) lv_obj_add_flag(ui_TideMarker, LV_OBJ_FLAG_HIDDEN);
                if (ui_TideCanvas) lv_obj_add_flag(ui_TideCanvas, LV_OBJ_FLAG_HIDDEN);

        tide_set_status("Tide: invalid", lv_color_hex(0xFF3333));
        Serial.println("[UI] ui_mainscreen_set_tide_curve: invalid input");
        return;
    }

    // --- Compute the 12h window we care about ---

    const time_t nowUtc = time(nullptr);
    const uint32_t twelveHours = 12U * 3600U;

    // Full series time span
    const time_t seriesStart = firstSampleUtc;
    const time_t seriesEnd   =
        firstSampleUtc + (time_t)(count - 1U) * (time_t)stepSeconds;

    // Target window [windowStart, windowEnd] = [now, now+12h], clamped to available data
    time_t windowStart = nowUtc;
    time_t windowEnd   = nowUtc + (time_t)twelveHours;

    if (windowStart < seriesStart) windowStart = seriesStart;
    if (windowEnd   > seriesEnd)   windowEnd   = seriesEnd;

    // Fallback: if for some reason the window collapses, just use the full series
    if (windowStart >= windowEnd) {
        windowStart = seriesStart;
        windowEnd   = seriesEnd;
    }

    // Convert window times to sample indices
    uint32_t startIndex = 0;
    uint32_t endIndex   = (uint32_t)(count - 1U);

    if (windowStart > seriesStart) {
        startIndex = (uint32_t)((windowStart - seriesStart) / (time_t)stepSeconds);
        if (startIndex >= count) startIndex = count - 1U;
    }

    if (windowEnd < seriesEnd) {
        endIndex = (uint32_t)((windowEnd - seriesStart) / (time_t)stepSeconds);
        if (endIndex >= count) endIndex = count - 1U;
    }

    if (endIndex <= startIndex) {
        // Ensure at least 2 samples
        if (startIndex == 0) {
            endIndex = (count > 1U) ? 1U : 0U;
        } else {
            startIndex = endIndex - 1U;
        }
    }

    uint16_t subCount = (uint16_t)(endIndex - startIndex + 1U);
    if (subCount < 2) {
        tideCurveValid     = false;
        tideSampleCount    = 0;
        tideFirstSampleUtc = 0;
        tideSampleStepSec  = 0;

        if (ui_TideLine)   lv_obj_add_flag(ui_TideLine, LV_OBJ_FLAG_HIDDEN);
        if (ui_TideMarker) lv_obj_add_flag(ui_TideMarker, LV_OBJ_FLAG_HIDDEN);

        tide_set_status("Tide: window too small", lv_color_hex(0xFF3333));
        Serial.println("[UI] ui_mainscreen_set_tide_curve: window too small");
        return;
    }

    // Clamp to our local buffer size
    if (subCount > TIDE_MAX_SAMPLES) {
        subCount  = TIDE_MAX_SAMPLES;
        endIndex  = startIndex + (uint32_t)subCount - 1U;
    }

    // Copy only the window we care about into tideSamples[]
    for (uint16_t i = 0; i < subCount; ++i) {
        tideSamples[i] = heights[startIndex + i];
    }

    tideSampleCount    = subCount;
    tideFirstSampleUtc = seriesStart + (time_t)startIndex * (time_t)stepSeconds;
    tideSampleStepSec  = stepSeconds;
    tideCurveValid     = true;

    Serial.printf(
        "[UI] ui_mainscreen_set_tide_curve: %u samples (idx %u..%u), step=%u s\n",
        (unsigned)tideSampleCount,
        (unsigned)startIndex,
        (unsigned)endIndex,
        (unsigned)tideSampleStepSec
    );

    draw_tide_curve();
    update_tide_marker();
}




void update_main_screen(void) {
    if (!ui_MainScreen || !lv_obj_is_valid(ui_MainScreen)) {
        return;
    }

    // --- Date + digital time ---
    if (ui_DateArcLabel && ui_TimeArcLabel) {
        time_t now = time(NULL);
        struct tm t;
        localtime_r(&now, &t);

        static char date_buf[24]; // e.g. "Tue 06-01-2026"
        static char time_buf[16]; // e.g. "00:58:12"

        strftime(date_buf, sizeof(date_buf), "%a %d-%m-%Y", &t);
        strftime(time_buf, sizeof(time_buf), "%H:%M:%S", &t);

        lv_arclabel_set_text_static(ui_DateArcLabel, date_buf);
        lv_arclabel_set_text_static(ui_TimeArcLabel, time_buf);
    }

    // --- Analogue arcs ---
    lv_arc_set_value(second_arc, second_value);
    lv_arc_set_value(minute_arc, minute_value);
    lv_arc_set_value(hour_arc,   hour_value % 12); // 0..11

        // --- Tide ring ---
    //
    // 1) If we haven't yet built a local tide curve (tideCurveValid == false),
    //    keep trying to pull one from WeatherManager each tick until it works.
    //
    // 2) After that, only rebuild when WeatherManager says the data changed
    //    via the dirty flag.
    //
    bool needTideRefresh = false;

    if (!tideCurveValid) {
        // First-time initialisation: UI has no curve yet.
        needTideRefresh = true;
    } else if (WeatherManager_TakeTideCurveDirtyFlag()) {
        // Subsequent updates: new tide data arrived.
        needTideRefresh = true;
    }

    if (needTideRefresh) {
        float    heights[TIDE_MAX_SAMPLES];
        uint16_t count       = 0;
        time_t   firstSample = 0;
        uint32_t stepSeconds = 0;

        if (WeatherManager_GetTideCurve(heights,
                                        TIDE_MAX_SAMPLES,
                                        count,
                                        firstSample,
                                        stepSeconds)) {

            // This fills tideSamples[], sets tideCurveValid = true,
            // and calls draw_tide_curve() + update_tide_marker().
            ui_mainscreen_set_tide_curve(heights,
                                         count,
                                         firstSample,
                                         stepSeconds);
        } else {
            // No curve yet; leave the "no data" cells visible.
            // (draw_tide_segments() handles that case.)
        }
    }

    // --- Tide marker: move every tick based on the stored tideSamples[] ---
    update_tide_marker();
}

void ui_mainscreen_apply_weather(uint16_t id, const char* tempText)
{
    if (ui_WeatherImage) {
        const char* iconPath = getMeteoconIcon(id, true);
        Serial.println("Icon path set in MainScreen UI");
        lv_img_set_src(ui_WeatherImage, iconPath);

      //  lv_color_t sci_fi_blue = lv_color_make(0, 200, 255);
      //  lv_obj_set_style_img_recolor(ui_WeatherImage, sci_fi_blue, LV_PART_MAIN);
      //  lv_obj_set_style_img_recolor_opa(ui_WeatherImage, LV_OPA_90, LV_PART_MAIN);
    }

    if (ui_WeatherLabel && tempText) {
        lv_label_set_text(ui_WeatherLabel, tempText);
    }
}