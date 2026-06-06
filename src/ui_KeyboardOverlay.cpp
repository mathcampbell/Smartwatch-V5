#include "ui_KeyboardOverlay.h"

#include <Arduino.h>

static constexpr int16_t SCREEN_W = 466;
static constexpr int16_t SCREEN_H = 466;

// Keep the whole editor comfortably inside the visible circular area.
// A full-screen rectangular backdrop looks wrong on this display, so the
// overlay is just an invisible click-catcher and the visible part is this
// centred rounded modal.
static constexpr int16_t MODAL_W = 420;
static constexpr int16_t MODAL_H = 320;
static constexpr int16_t MODAL_X = (SCREEN_W - MODAL_W) / 2;
static constexpr int16_t MODAL_Y_SHOWN = (SCREEN_H - MODAL_H) / 2;
static constexpr int16_t MODAL_Y_HIDDEN = SCREEN_H + 8;

static constexpr int16_t PREVIEW_X = 20;
static constexpr int16_t PREVIEW_Y = 18;
static constexpr int16_t PREVIEW_W = MODAL_W - 40;
static constexpr int16_t PREVIEW_H = 54;

static constexpr int16_t KEYBOARD_X = 8;
static constexpr int16_t KEYBOARD_Y = 88;
static constexpr int16_t KEYBOARD_W = MODAL_W - 16;
static constexpr int16_t KEYBOARD_H = 214;

static lv_obj_t* s_screen = nullptr;
static lv_obj_t* s_overlay = nullptr;
static lv_obj_t* s_modal = nullptr;
static lv_obj_t* s_preview = nullptr;
static lv_obj_t* s_keyboard = nullptr;
static lv_obj_t* s_activeTextarea = nullptr;
static lv_obj_t* s_activeScrollParent = nullptr;
static bool s_visible = false;

static void modal_y_anim_cb(void* obj, int32_t y)
{
    lv_obj_set_y((lv_obj_t*)obj, y);
}

static void overlay_delete_ready_cb(lv_anim_t* a)
{
    (void)a;
    if (s_overlay) {
        lv_obj_del(s_overlay);
        s_overlay = nullptr;
        s_modal = nullptr;
        s_preview = nullptr;
        s_keyboard = nullptr;
        s_visible = false;
    }
}

static void copy_preview_to_target()
{
    if (!s_preview || !s_activeTextarea) return;
    lv_textarea_set_text(s_activeTextarea, lv_textarea_get_text(s_preview));
}

static void copy_target_to_preview()
{
    if (!s_preview || !s_activeTextarea) return;
    lv_textarea_set_text(s_preview, lv_textarea_get_text(s_activeTextarea));
}

static void keyboard_event_cb(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_VALUE_CHANGED) {
        copy_preview_to_target();
        return;
    }

    if (code == LV_EVENT_READY) {
        copy_preview_to_target();
        lv_obj_t* ta = s_activeTextarea;
        if (ta) {
            lv_obj_send_event(ta, LV_EVENT_READY, nullptr);
            lv_obj_clear_state(ta, LV_STATE_FOCUSED);
        }
        ui_keyboard_hide();
        return;
    }

    if (code == LV_EVENT_CANCEL) {
        if (s_activeTextarea) lv_obj_clear_state(s_activeTextarea, LV_STATE_FOCUSED);
        ui_keyboard_hide();
    }
}

static void overlay_bg_event_cb(lv_event_t* e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        copy_preview_to_target();
        if (s_activeTextarea) lv_obj_clear_state(s_activeTextarea, LV_STATE_FOCUSED);
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
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_overlay, overlay_bg_event_cb, LV_EVENT_CLICKED, nullptr);

    s_modal = lv_obj_create(s_overlay);
    lv_obj_remove_style_all(s_modal);
    lv_obj_set_size(s_modal, MODAL_W, MODAL_H);
    lv_obj_set_pos(s_modal, MODAL_X, MODAL_Y_HIDDEN);
    lv_obj_set_style_bg_color(s_modal, lv_color_hex(0x151827), 0);
    lv_obj_set_style_bg_opa(s_modal, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_modal, 30, 0);
    lv_obj_set_style_border_width(s_modal, 1, 0);
    lv_obj_set_style_border_color(s_modal, lv_color_hex(0x435074), 0);
    lv_obj_set_style_shadow_width(s_modal, 18, 0);
    lv_obj_set_style_shadow_opa(s_modal, LV_OPA_40, 0);
    lv_obj_set_style_shadow_color(s_modal, lv_color_hex(0x000000), 0);
    lv_obj_clear_flag(s_modal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_modal, LV_OBJ_FLAG_CLICKABLE);

    s_preview = lv_textarea_create(s_modal);
    lv_obj_set_size(s_preview, PREVIEW_W, PREVIEW_H);
    lv_obj_set_pos(s_preview, PREVIEW_X, PREVIEW_Y);
    lv_textarea_set_one_line(s_preview, true);
    lv_textarea_set_placeholder_text(s_preview, "Text");
    lv_obj_set_style_text_font(s_preview, &lv_font_montserrat_20, 0);
    lv_obj_set_style_radius(s_preview, 18, 0);
    lv_obj_set_style_bg_color(s_preview, lv_color_hex(0x232A3D), 0);
    lv_obj_set_style_text_color(s_preview, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(s_preview, lv_color_hex(0x5B6FA3), 0);
    lv_obj_set_style_border_width(s_preview, 1, 0);
    lv_obj_set_style_pad_all(s_preview, 10, 0);

    s_keyboard = lv_keyboard_create(s_modal);
    lv_obj_set_size(s_keyboard, KEYBOARD_W, KEYBOARD_H);
    lv_obj_set_pos(s_keyboard, KEYBOARD_X, KEYBOARD_Y);
    lv_keyboard_set_mode(s_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(s_keyboard, s_preview);
    lv_obj_add_event_cb(s_keyboard, keyboard_event_cb, LV_EVENT_ALL, nullptr);

    lv_obj_move_foreground(s_overlay);
}

static void show_keyboard(lv_obj_t* textarea, lv_obj_t* scroll_parent)
{
    if (!textarea) return;

    s_activeTextarea = textarea;
    s_activeScrollParent = scroll_parent;

    create_overlay_if_needed();
    if (!s_keyboard || !s_modal || !s_preview) return;

    copy_target_to_preview();
    lv_keyboard_set_textarea(s_keyboard, s_preview);
    lv_obj_move_foreground(s_overlay);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_modal);
    lv_anim_set_values(&a, lv_obj_get_y(s_modal), MODAL_Y_SHOWN);
    lv_anim_set_duration(&a, 180);
    lv_anim_set_exec_cb(&a, modal_y_anim_cb);
    lv_anim_start(&a);

    lv_obj_add_state(s_preview, LV_STATE_FOCUSED);
    s_visible = true;
}

void ui_keyboard_hide(void)
{
    if (!s_overlay || !s_modal) return;

    if (s_keyboard) lv_keyboard_set_textarea(s_keyboard, nullptr);

    s_activeTextarea = nullptr;
    s_activeScrollParent = nullptr;

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_modal);
    lv_anim_set_values(&a, lv_obj_get_y(s_modal), MODAL_Y_HIDDEN);
    lv_anim_set_duration(&a, 160);
    lv_anim_set_exec_cb(&a, modal_y_anim_cb);
    lv_anim_set_completed_cb(&a, overlay_delete_ready_cb);
    lv_anim_start(&a);
}

static void textarea_focus_event_cb(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* textarea = (lv_obj_t*)lv_event_get_target(e);
    lv_obj_t* scroll_parent = (lv_obj_t*)lv_event_get_user_data(e);
    (void)scroll_parent;

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
