#pragma once

#include <lvgl.h>

// Reusable keyboard overlay for the 466x466 round display.
// Attach any textarea to get a bottom sliding keyboard panel and automatic
// scroll-to-visible behaviour for the active field.
void ui_keyboard_overlay_init(lv_obj_t* screen);
void ui_keyboard_attach_textarea(lv_obj_t* textarea, lv_obj_t* scroll_parent);
void ui_keyboard_hide(void);
