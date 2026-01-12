#pragma once
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Global screen pointer, consistent with Squareline style
extern lv_obj_t * ui_WeatherScreen;

// Create the screen (call once from ui_init or setup)
void ui_WeatherScreen_screen_init(void);

// Call when screen is active; updates background + starts/stops FX on weather changes
void ui_WeatherScreen_tick(void);

#ifdef __cplusplus
}
#endif
