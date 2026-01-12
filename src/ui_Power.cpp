#include "ui_Power.h"
#include <Arduino.h>
#include <math.h>
#include "ui.h"

// Screen
lv_obj_t * ui_Power = nullptr;

// UI objects
static lv_obj_t * ui_PowerRadialRing = nullptr;
static lv_obj_t * ui_ShutdownArc     = nullptr;
static lv_obj_t * ui_ShutdownKnob    = nullptr;
static lv_obj_t * ui_TitleLabel      = nullptr;
static lv_obj_t * ui_HintLabel       = nullptr;
static lv_obj_t * ui_ShutdownIconWrap = nullptr;  // circular overlay that follows the knob
static lv_obj_t * ui_ShutdownIconLbl  = nullptr;  // red power symbol inside it


static lv_obj_t * btn_cancel         = nullptr;
static lv_obj_t * btn_restart        = nullptr;

// Geometry (412x412)
static constexpr int SCREEN_W = 466;
static constexpr int SCREEN_H = 466;
static constexpr int CX = SCREEN_W / 2;
static constexpr int CY = SCREEN_H / 2;

// Colours (match your existing look)
static constexpr uint32_t COL_BG       = 0x09001a;
static constexpr uint32_t COL_RING     = 0x2c2836;
static constexpr uint32_t COL_TEXT     = 0xFFFFFF;
static constexpr uint32_t COL_ACCENT   = 0x41C7FF;
static constexpr uint32_t COL_DANGER   = 0xD02020;
static constexpr uint32_t COL_RING_DIM = 0x1c1826;

// Shutdown slider arc config: symmetric bottom 1/4 circle.
// On your build, 45..135 places it at the bottom.
static constexpr int ARC_LEFT_DEG  = 45;   // left end of bottom segment
static constexpr int ARC_RIGHT_DEG = 135;  // right end of bottom segment

// Slider starts on the RIGHT end, must drag to LEFT end to trigger
static constexpr int SLIDER_START_DEG = ARC_RIGHT_DEG;  // right end
static constexpr int SLIDER_END_DEG   = ARC_LEFT_DEG;   // left end

// How close to the left end counts as "completed"
static constexpr int TRIGGER_ZONE_DEG = 10;  // degrees from left end

// Knob travel radius (centerline of the thick arc band)
static constexpr int KNOB_RADIUS = 175;

// Drag gating
static bool slide_started_ = false;
static bool slide_armed_   = false;

// Helpers
static inline float deg2rad(float d) { return d * 3.14159265f / 180.0f; }

static int clampi(int v, int lo, int hi)
{
    if(v < lo) return lo;
    if(v > hi) return hi;
    return v;
}


// Map a clamped angle in [ARC_LEFT_DEG..ARC_RIGHT_DEG] to [0..100]
static int clamp_angle(int a, int lo, int hi) {
    if(a < lo) return lo;
    if(a > hi) return hi;
    return a;
}

static int value_to_angle(int v)
{
    v = clampi(v, 0, 100);
    return ARC_LEFT_DEG + (v * (ARC_RIGHT_DEG - ARC_LEFT_DEG)) / 100;
}

static void set_hint(const char *txt)
{
    if(ui_HintLabel) lv_label_set_text(ui_HintLabel, txt);
}

static void set_knob_by_value(int progress)
{
    // progress: 0 = RIGHT end, 100 = LEFT end
    progress = clampi(progress, 0, 100);

    // Convert progress to angle on the bottom segment [ARC_LEFT_DEG..ARC_RIGHT_DEG]
    // Right end => ARC_RIGHT_DEG, Left end => ARC_LEFT_DEG
    const int span = (ARC_RIGHT_DEG - ARC_LEFT_DEG);
    const int a = ARC_RIGHT_DEG - (progress * span) / 100;

    const float r = deg2rad((float)a);

    const int x = (int)(CX + KNOB_RADIUS * cosf(r));
    const int y = (int)(CY + KNOB_RADIUS * sinf(r));

    const int ks = lv_obj_get_width(ui_ShutdownKnob);
    lv_obj_set_pos(ui_ShutdownKnob, x - ks/2, y - ks/2);

    // With LV_ARC_MODE_REVERSE, increasing value fills from right->left (end->start)
    lv_arc_set_value(ui_ShutdownArc, progress);
}


// Segment-style icon button like your Settings screen
static lv_obj_t * make_segment_btn(lv_obj_t * parent, const char *symbol)
{
    lv_obj_t * btn = lv_btn_create(parent);

    lv_obj_set_size(btn, 54, 54);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);

    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_TRANSP, 0);

    lv_obj_t * lbl = lv_label_create(btn);
    lv_label_set_text(lbl, symbol);
    lv_obj_center(lbl);
    lv_obj_set_style_text_color(lbl, lv_color_hex(COL_ACCENT), 0);

    // ensure no scrollbars/scrolling weirdness
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(btn, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(lbl, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(lbl, LV_SCROLLBAR_MODE_OFF);

    return btn;
}

static void place_btn_by_angle(lv_obj_t * btn, int deg, int radius)
{
    float r = deg2rad((float)deg);
    int x = (int)(radius * cosf(r));
    int y = (int)(radius * sinf(r));
    lv_obj_align(btn, LV_ALIGN_CENTER, x, y);
}

// Button event callback
static void btn_event_cb(lv_event_t * e)
{
    const uintptr_t tag = (uintptr_t)lv_event_get_user_data(e);

    if(tag == 1) {
        ui_action_power_cancel();
    } else if(tag == 2) {
        ui_action_power_restart();
    }
}


static void shutdown_icon_follow_arc(void)
{
    if(!ui_ShutdownArc || !ui_ShutdownIconWrap) return;

    // radius_offset = 0 aligns to the knob radius.
    // If you want it slightly inward/outward relative to the knob, tweak ± pixels.
    lv_arc_align_obj_to_angle(ui_ShutdownArc, ui_ShutdownIconWrap, 0);
}

static void shutdown_arc_event_cb(lv_event_t * e)
{
    lv_obj_t * arc = (lv_obj_t *)lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);

    if(code == LV_EVENT_VALUE_CHANGED) {
        shutdown_icon_follow_arc();

        int v = lv_arc_get_value(arc); // 0..100
        if(v >= 95) set_hint("Release to power off");
        else        set_hint("Slide to power off");
        return;
    }

    if(code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        int v = lv_arc_get_value(arc);

        if(v >= 95) {
            ui_action_power_shutdown();
            return;
        }

        // Snap back to start
        set_hint("Slide to power off");
        lv_arc_set_value(arc, 0);
        shutdown_icon_follow_arc();
        return;
    }
}


void ui_Power_screen_init(void)
{
    ui_Power = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_Power, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(ui_Power, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(ui_Power, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(ui_Power, LV_OPA_COVER, 0);

    // ---- Background radial ring (non-clickable) ----
    ui_PowerRadialRing = lv_arc_create(ui_Power);
    lv_obj_set_size(ui_PowerRadialRing, 420, 420);
    lv_obj_center(ui_PowerRadialRing);

    lv_arc_set_bg_angles(ui_PowerRadialRing, 0, 360);
    lv_arc_set_range(ui_PowerRadialRing, 0, 100);
    lv_arc_set_value(ui_PowerRadialRing, 0);

    lv_obj_set_style_arc_width(ui_PowerRadialRing, 50, LV_PART_MAIN);
    lv_obj_set_style_arc_color(ui_PowerRadialRing, lv_color_hex(COL_RING), LV_PART_MAIN);

    // pure ring
    lv_obj_set_style_arc_width(ui_PowerRadialRing, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(ui_PowerRadialRing, LV_OPA_TRANSP, LV_PART_KNOB);

    lv_obj_clear_flag(ui_PowerRadialRing, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(ui_PowerRadialRing, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(ui_PowerRadialRing, LV_SCROLLBAR_MODE_OFF);

    // ---- Title + hint ----
    ui_TitleLabel = lv_label_create(ui_Power);
    lv_label_set_text(ui_TitleLabel, "Power");
    lv_obj_set_style_text_color(ui_TitleLabel, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_text_align(ui_TitleLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(ui_TitleLabel, LV_ALIGN_CENTER, 0, -70);

    ui_HintLabel = lv_label_create(ui_Power);
    lv_label_set_text(ui_HintLabel, "Slide to power off");
    lv_obj_set_style_text_color(ui_HintLabel, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_text_align(ui_HintLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(ui_HintLabel, LV_ALIGN_CENTER, 0, 105);

    // ---- Top buttons (upper-left / upper-right) ----
    btn_cancel  = make_segment_btn(ui_Power, LV_SYMBOL_CLOSE);
    btn_restart = make_segment_btn(ui_Power, LV_SYMBOL_REFRESH);

    place_btn_by_angle(btn_cancel,  225, 175); // upper-left
    place_btn_by_angle(btn_restart, 315, 175); // upper-right

    lv_obj_add_event_cb(btn_cancel,  btn_event_cb, LV_EVENT_CLICKED, (void*)1);
    lv_obj_add_event_cb(btn_restart, btn_event_cb, LV_EVENT_CLICKED, (void*)2);

// ---- Shutdown arc (native LVGL slider) ----
ui_ShutdownArc = lv_arc_create(ui_Power);
lv_obj_set_size(ui_ShutdownArc, 420, 420);
lv_obj_center(ui_ShutdownArc);

// We only use a bottom segment
lv_arc_set_bg_angles(ui_ShutdownArc, 45, 135);   // bottom segment on your build

// Slider range
lv_arc_set_range(ui_ShutdownArc, 0, 100);

// Start value and direction
lv_arc_set_value(ui_ShutdownArc, 0);
lv_arc_set_mode(ui_ShutdownArc, LV_ARC_MODE_REVERSE);

// Thickness
lv_obj_set_style_arc_width(ui_ShutdownArc, 50, LV_PART_MAIN);
lv_obj_set_style_arc_width(ui_ShutdownArc, 50, LV_PART_INDICATOR);

// Track + filled section colours
lv_obj_set_style_arc_color(ui_ShutdownArc, lv_color_hex(0x1c1826), LV_PART_MAIN);       // track
lv_obj_set_style_arc_color(ui_ShutdownArc, lv_color_hex(0x646c7a), LV_PART_INDICATOR);  // filled (light grey)

// Make the *native* knob itself invisible (we'll draw our own overlay knob)
lv_obj_set_style_bg_opa(ui_ShutdownArc, LV_OPA_TRANSP, LV_PART_KNOB);
lv_obj_set_style_border_width(ui_ShutdownArc, 0, LV_PART_KNOB);
lv_obj_set_style_outline_width(ui_ShutdownArc, 0, LV_PART_KNOB);
lv_obj_set_style_shadow_width(ui_ShutdownArc, 0, LV_PART_KNOB);


// Make the knob *visually bigger* by adding padding on the knob part
lv_obj_set_style_pad_all(ui_ShutdownArc, 2, LV_PART_KNOB);

// Disable scrollbars/scrolling artefacts
lv_obj_clear_flag(ui_ShutdownArc, LV_OBJ_FLAG_SCROLLABLE);
lv_obj_set_scrollbar_mode(ui_ShutdownArc, LV_SCROLLBAR_MODE_OFF);

// Ensure it can be dragged
lv_obj_add_flag(ui_ShutdownArc, LV_OBJ_FLAG_CLICKABLE);
lv_obj_add_flag(ui_ShutdownArc, LV_OBJ_FLAG_ADV_HITTEST);

// Attach event handler
lv_obj_add_event_cb(ui_ShutdownArc, shutdown_arc_event_cb, LV_EVENT_ALL, NULL);

// --- Overlay knob (dark circle with white border) that follows the arc ---
ui_ShutdownIconWrap = lv_obj_create(ui_Power);
lv_obj_remove_style_all(ui_ShutdownIconWrap);
lv_obj_set_size(ui_ShutdownIconWrap, 46, 46);
lv_obj_set_style_radius(ui_ShutdownIconWrap, LV_RADIUS_CIRCLE, 0);

lv_obj_set_style_bg_color(ui_ShutdownIconWrap, lv_color_hex(0x0F1014), 0);  // near-black
lv_obj_set_style_bg_opa(ui_ShutdownIconWrap, LV_OPA_COVER, 0);
lv_obj_set_style_border_width(ui_ShutdownIconWrap, 2, 0);
lv_obj_set_style_border_color(ui_ShutdownIconWrap, lv_color_hex(0xBABABA), 0);
lv_obj_set_style_outline_width(ui_ShutdownIconWrap, 0, 0);
lv_obj_set_style_shadow_width(ui_ShutdownIconWrap, 0, 0);

// Don't let it intercept dragging; the arc should receive input
lv_obj_clear_flag(ui_ShutdownIconWrap, LV_OBJ_FLAG_CLICKABLE);
lv_obj_clear_flag(ui_ShutdownIconWrap, LV_OBJ_FLAG_SCROLLABLE);
lv_obj_set_scrollbar_mode(ui_ShutdownIconWrap, LV_SCROLLBAR_MODE_OFF);

// Red power symbol centered
ui_ShutdownIconLbl = lv_label_create(ui_ShutdownIconWrap);
lv_label_set_text(ui_ShutdownIconLbl, LV_SYMBOL_POWER);
lv_obj_center(ui_ShutdownIconLbl);
lv_obj_set_style_text_color(ui_ShutdownIconLbl, lv_color_hex(0xD02020), 0);

// Position it initially
shutdown_icon_follow_arc();


}

