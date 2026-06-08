#include "ui_Settings.h"

#include <Arduino.h>
#include <math.h>

extern lv_obj_t * ui_SettingsRadialMenu;

static constexpr int16_t SETTINGS_SCREEN_SIZE = 466;
static constexpr int16_t SETTINGS_RING_SIZE = 460;
static constexpr int16_t SETTINGS_RING_WIDTH = 50;
static constexpr int16_t SETTINGS_CONTENT_SIZE = 340;
static constexpr int16_t SETTINGS_ICON_RADIUS = 205;
static constexpr int16_t SETTINGS_ICON_BUTTON_SIZE = 40;

void ui_Settings_apply_compact_radial_layout(void)
{
    if (ui_SettingsRadialMenu && lv_obj_is_valid(ui_SettingsRadialMenu)) {
        lv_obj_set_size(ui_SettingsRadialMenu, SETTINGS_RING_SIZE, SETTINGS_RING_SIZE);
        lv_obj_center(ui_SettingsRadialMenu);
        lv_obj_set_style_arc_width(ui_SettingsRadialMenu, SETTINGS_RING_WIDTH, LV_PART_MAIN);
        lv_obj_set_style_arc_width(ui_SettingsRadialMenu, SETTINGS_RING_WIDTH, LV_PART_INDICATOR);
        lv_arc_set_bg_angles(ui_SettingsRadialMenu, 0, 360);
        lv_arc_set_rotation(ui_SettingsRadialMenu, -90);
    }

    if (content_area && lv_obj_is_valid(content_area)) {
        lv_obj_set_size(content_area, SETTINGS_CONTENT_SIZE, SETTINGS_CONTENT_SIZE);
        lv_obj_center(content_area);
        lv_obj_set_style_radius(content_area, LV_RADIUS_CIRCLE, 0);
    }

    const int angle_per_segment = 360 / NUM_SEGMENTS;
    for (int i = 0; i < NUM_SEGMENTS; ++i) {
        if (!arc_segments[i] || !lv_obj_is_valid(arc_segments[i])) continue;

        int mid_angle = i * angle_per_segment + angle_per_segment / 2;
        mid_angle -= 90;
        if (mid_angle < 0) mid_angle += 360;

        const float rad = mid_angle * 3.14159265f / 180.0f;
        const int x = (int)(SETTINGS_ICON_RADIUS * cosf(rad));
        const int y = (int)(SETTINGS_ICON_RADIUS * sinf(rad));

        lv_obj_set_size(arc_segments[i], SETTINGS_ICON_BUTTON_SIZE, SETTINGS_ICON_BUTTON_SIZE);
        lv_obj_align(arc_segments[i], LV_ALIGN_CENTER, x, y);
        lv_obj_move_foreground(arc_segments[i]);
    }

    Serial.println("[SettingsLayout] Applied compact 50px radial menu layout");
}
