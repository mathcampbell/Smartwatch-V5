// ui_mainscreen.h
#ifndef UI_MAINSCREEN_H
#define UI_MAINSCREEN_H

#include "lvgl.h"

void ui_MainScreen_screen_init(void);
void update_main_screen(void);
void create_combined_scale(void);
void create_segmented_ring(lv_obj_t * parent);
void ui_mainscreen_apply_weather(uint16_t id, const char* tempText);

// Feed tide samples into the main screen tide ring.
// See definition in ui_MainScreen.cpp for full docs.
void ui_mainscreen_set_tide_curve(const float *heights,
                                  uint16_t     count,
                                  time_t       firstSampleUtc,
                                  uint32_t     stepSeconds);

#endif // MAINSCREEN_H
