#pragma once
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern lv_obj_t * ui_WeatherScreen;

void ui_WeatherScreen_screen_init(void);
void ui_WeatherScreen_tick(void);

#ifdef __cplusplus
}
#endif
