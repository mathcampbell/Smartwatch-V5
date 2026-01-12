// ui_Settings.h

#ifndef UI_SETTINGS_H
#define UI_SETTINGS_H

#include "lvgl.h"
#include <WiFi.h> // Include Wi-Fi library
#include "ui.h"
//#include "PowerManager.h"
#include "SettingsManager.h"

// Number of menu items
#define NUM_SEGMENTS 6

// Extern variables
extern lv_obj_t * ui_Settings;
extern lv_obj_t * arc_segments[NUM_SEGMENTS];
extern const char * segment_labels[NUM_SEGMENTS];
extern const char * segment_symbols[NUM_SEGMENTS];
extern lv_obj_t * content_area;
//extern PowerManager powerManager;



// Function prototypes
void ui_Settings_screen_init(void);
void create_radial_menu(void);
void create_content_area(void);
void arc_event_cb(lv_event_t * e);
void add_segment_buttons(void);
void segment_btn_event_cb(lv_event_t * e);
void show_wifi_settings(void);
void show_brightness_settings(void);
void show_sleep_settings(void);
void show_general_settings(void);
void show_bluetooth_settings(void);
void show_weather(void);
void show_sound_settings(void);
void add_segment_labels(void);
void show_radial_menu_and_buttons(void);
void hide_radial_menu_and_buttons(void); 


void brightness_slider_event_cb(lv_event_t * e);
void brightness_slider_released_event_cb(lv_event_t * e);
void sleep_timer_slider_event_cb(lv_event_t * e);
void wifi_switch_event_cb(lv_event_t * e);
void wifi_network_selected_cb(lv_event_t * e);
void saved_network_selected_cb(lv_event_t * e);
void save_wifi_network_event_cb(lv_event_t * e);
void edit_saved_network_event_cb(lv_event_t * e);
void remove_saved_network_event_cb(lv_event_t * e);
void connect_wifi_network_event_cb(lv_event_t * e);
void settingsMenu_valuechange(lv_event_t * e);
void settingsMenu_select(lv_event_t * e);
void save_updated_password_event_cb(lv_event_t * e);

void wifi_enable(void);
void wifi_disable(void);
void scan_and_display_wifi_networks(void);

#endif // UI_SETTINGS_H