#include "ui_KeyboardOverlay.h"

#include <Arduino.h>

static constexpr int16_t SCREEN_W = 466;
static constexpr int16_t SCREEN_H = 466;
static constexpr int16_t PANEL_H  = 218;
static constexpr int16_t PANEL_Y_SHOWN = SCREEN_H - PANEL_H;
static constexpr int16_t PANEL_Y_HIDDEN = SCREEN_H + 8;

static lv_obj_t* s_screen = nullptr;
static lv_obj_t* s_overlay = nullptr;
static lv_obj_t* s_panel = nullptr;
static lv_obj_t* s_keyboard = nullptr;
static lv_obj_t* s_activeTextarea = nullptr;
static lv_obj_t* s_activeScrollParent = nullptr;
static bool s_visible = false;

static void panel_y_anim_cb(void* obj, int32_t y)
{
    lv_obj_set_y((lv_obj_t*)obj, y);
}

static void overlay_delete_ready_cb(lv_anim_t* a)
{
    (void)a;
    if (s_overlay) {
        lv_obj_del(s_overlay);
        s_overlay = nullptr;
        s_panel = nullptr;
        s_keyboard = nullptr;
        s_visible = false;
    }
}

static void keep_active_textarea_visible()
{
    if (!s_activeTextarea) return;

    if (s_activeScrollParent) {
        lv_obj_scroll_to_view_recursive(s_activeTextarea, LV_ANIM_ON);
    } else {
        lv_obj_scroll_to_view_recursive(s_activeTextarea, LV_ANIM_ON);
    }
}

static void keyboard_event_cb(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        if (s_activeTextarea) {
            lv_obj_clear_state(s_activeTextarea, LV_STATE_FOCUSED);
        }
        ui_keyboard_hide();
    }
}

static void overlay_bg_event_cb(lv_event_t* e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        if (s_activeTextarea) {
            lv_obj_clear_state(s_activeTextarea, LV_STATE_FOCUSED);
        }
        ui_keyboard_hide();
    }
}

static void create_overlay_if_needed()
{
    if (s_overlay) return;

    lv_obj_t* parent = s_screen ? s_screen : lv_screen_active();

    s_overlay = lv_obj_create(parent);
    lv_obj_remove_style_all(s_overlay);
    lv_obj_set_size(s_overlay, SCREEN_W, SCREEN_H);
    lv_obj_align(s_overlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_50, 0);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_overlay, overlay_bg_event_cb, LV_EVENT_CLICKED, nullptr);

    s_panel = lv_obj_create(s_overlay);
    lv_obj_remove_style_all(s_panel);
    lv_obj_set_size(s_panel, SCREEN_W, PANEL_H);
    lv_obj_set_pos(s_panel, 0, PANEL_Y_HIDDEN);
    lv_obj_set_style_bg_color(s_panel, lv_color_hex(0x151827), 0);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_panel, 22, 0);
    lv_obj_set_style_border_width(s_panel, 1, 0);
    lv_obj_set_style_border_color(s_panel, lv_color_hex(0x35405F), 0);
    lv_obj_set_style_pad_all(s_panel, 8, 0);
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_CLICKABLE);

    s_keyboard = lv_keyboard_create(s_panel);
    lv_obj_set_size(s_keyboard, SCREEN_W - 16, PANEL_H - 16);
    lv_obj_align(s_keyboard, LV_ALIGN_CENTER, 0, 0);
    lv_keyboard_set_mode(s_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_add_event_cb(s_keyboard, keyboard_event_cb, LV_EVENT_ALL, nullptr);

    lv_obj_move_foreground(s_overlay);
}

static void show_keyboard(lv_obj_t* textarea, lv_obj_t* scroll_parent)
{
    if (!textarea) return;

    s_activeTextarea = textarea;
    s_activeScrollParent = scroll_parent;

    create_overlay_if_needed();
    if (!s_keyboard || !s_panel) return;

    lv_keyboard_set_textarea(s_keyboard, textarea);
    lv_obj_move_foreground(s_overlay);
    keep_active_textarea_visible();

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_panel);
    lv_anim_set_values(&a, lv_obj_get_y(s_panel), PANEL_Y_SHOWN);
    lv_anim_set_duration(&a, 180);
    lv_anim_set_exec_cb(&a, panel_y_anim_cb);
    lv_anim_start(&a);

    s_visible = true;
}

void ui_keyboard_hide(void)
{
    if (!s_overlay || !s_panel) return;

    if (s_keyboard) {
        lv_keyboard_set_textarea(s_keyboard, nullptr);
    }

    s_activeTextarea = nullptr;
    s_activeScrollParent = nullptr;

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_panel);
    lv_anim_set_values(&a, lv_obj_get_y(s_panel), PANEL_Y_HIDDEN);
    lv_anim_set_duration(&a, 160);
    lv_anim_set_exec_cb(&a, panel_y_anim_cb);
    lv_anim_set_completed_cb(&a, overlay_delete_ready_cb);
    lv_anim_start(&a);
}

static void textarea_focus_event_cb(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* textarea = (lv_obj_t*)lv_event_get_target(e);
    lv_obj_t* scroll_parent = (lv_obj_t*)lv_event_get_user_data(e);

    if (code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED) {
        show_keyboard(textarea, scroll_parent);
    }
}

void ui_keyboard_overlay_init(lv_obj_t* screen)
{
    s_screen = screen;
}

void ui_keyboard_attach_textarea(lv_obj_t* textarea, lv_obj_t* scroll_parent)
{
    if (!textarea) return;
    lv_obj_add_event_cb(textarea, textarea_focus_event_cb, LV_EVENT_FOCUSED, scroll_parent);
    lv_obj_add_event_cb(textarea, textarea_focus_event_cb, LV_EVENT_CLICKED, scroll_parent);
}
