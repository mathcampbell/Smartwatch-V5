#pragma once

#include <lvgl.h>

// Adds the weather/tide location picker to an existing settings container.
// Intended to be called from show_general_settings() after the existing
// brightness/sleep controls have been created.
void ui_settings_add_location_controls(lv_obj_t* parent);
