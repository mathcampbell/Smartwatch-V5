// ui_clockscreen.h


#include "lvgl.h"

void ui_ClockScreen_screen_init(void);
void update_clock_screen(void);
static void alarm_stop_bubble_cb(lv_event_t * e);
static void alarm_update_bell_style(void);
static void alarm_bell_longpress_cb(lv_event_t * e);