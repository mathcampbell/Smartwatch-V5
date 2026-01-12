#include "ui.h"
#include <Arduino.h>
#include "clock.h"
#include "ui_MainScreen.h"
#include <lvgl.h>
#include "esp_heap_caps.h" // Include this header for heap_caps_malloc


#include <time.h>


lv_obj_t * second_arc;
lv_obj_t * minute_arc;
lv_obj_t * hour_arc;

// Mid-ring + arc labels (date + digital time)
static lv_obj_t * ui_MidInfoRing = NULL;
static lv_obj_t * ui_DateArcLabel = NULL;
static lv_obj_t * ui_TimeArcLabel = NULL;

#define CANVAS_WIDTH  466
#define CANVAS_HEIGHT 466

extern const char* getMeteoconIcon(uint16_t weatherId, bool day);



void ui_MainScreen_screen_init(void)
{
    ui_MainScreen = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_MainScreen, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_bg_color(ui_MainScreen, lv_color_hex(0x01070f), LV_PART_MAIN | LV_STATE_DEFAULT);
    //lv_obj_set_style_bg_color(ui_MainScreen, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_MainScreen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(ui_MainScreen, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_main_stop(ui_MainScreen, 100, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_stop(ui_MainScreen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    
      // Initialize the main arc menu with glow effect on the indicator
    ui_MainArcMenu = lv_arc_create(ui_MainScreen);
    lv_obj_set_width(ui_MainArcMenu, 200);
    lv_obj_set_height(ui_MainArcMenu, 200);
    lv_obj_set_align(ui_MainArcMenu, LV_ALIGN_CENTER);
    lv_arc_set_range(ui_MainArcMenu, 0, 500);
    lv_arc_set_value(ui_MainArcMenu, 100);
    lv_obj_set_style_arc_width(ui_MainArcMenu, 10, LV_PART_MAIN);
    
    lv_obj_set_style_arc_color(ui_MainArcMenu, lv_color_hex(0x01070f), LV_PART_MAIN);

    lv_obj_set_style_arc_color(ui_MainArcMenu, lv_color_hex(0x79CBFC), LV_PART_INDICATOR);

    lv_obj_remove_style(ui_MainArcMenu, NULL, LV_PART_KNOB); // Remove knob
     lv_obj_set_style_arc_width(ui_MainArcMenu, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(ui_MainArcMenu, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(ui_MainArcMenu, false, LV_PART_INDICATOR | LV_STATE_DEFAULT);
   
    // Main arc style
    static lv_style_t style_main_arc;
    lv_style_init(&style_main_arc);

    lv_style_set_arc_opa(&style_main_arc, 255);
    lv_style_set_arc_rounded(&style_main_arc, false); // Ensuring no rounded ends for main arc

    // Indicator arc style with a more intense glow effect
    static lv_style_t style_indicator_glow;
    lv_style_init(&style_indicator_glow);
    lv_style_set_arc_color(&style_indicator_glow, lv_color_hex(0x79CBFC));
    lv_style_set_arc_opa(&style_indicator_glow, 255);
    lv_style_set_arc_width(&style_indicator_glow, 10); // Increased width for visibility
    lv_style_set_arc_rounded(&style_indicator_glow, false); // Ensuring no rounded ends for indicator
   // lv_style_set_shadow_color(&style_indicator_glow, lv_color_hex(0x79CBFC));
    //lv_style_set_shadow_opa(&style_indicator_glow, 50);
    //lv_style_set_shadow_width(&style_indicator_glow, 20); // Increased shadow width for stronger glow
    //lv_style_set_shadow_offset_x(&style_indicator_glow, 10);
    //lv_style_set_shadow_offset_y(&style_indicator_glow, 10);
    //lv_style_set_shadow_spread(&style_indicator_glow, 15);
    //lv_style_set_radius(&style_indicator_glow, 100); // No rounding

    // Apply the styles to the arc
    lv_obj_add_style(ui_MainArcMenu, &style_main_arc, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_style(ui_MainArcMenu, &style_indicator_glow, LV_PART_INDICATOR | LV_STATE_DEFAULT | LV_STATE_PRESSED);

    ui_BatteryArc = lv_arc_create(ui_MainScreen);
    lv_obj_set_size(ui_BatteryArc, 170, 170);
    lv_obj_align(ui_BatteryArc, LV_ALIGN_CENTER, 0, 0);
    lv_arc_set_bg_angles(ui_BatteryArc, 0, 360);
    lv_arc_set_rotation(ui_BatteryArc, 270); // Start from top
    lv_arc_set_value(ui_BatteryArc, 100); // Initially 100%
    lv_obj_remove_style(ui_BatteryArc, NULL, LV_PART_KNOB); // Remove knob
    lv_obj_clear_flag(ui_BatteryArc, LV_OBJ_FLAG_CLICKABLE); // Non-clickable

    // Set initial style
    lv_obj_set_style_arc_width(ui_BatteryArc, 5, LV_PART_MAIN);
    lv_obj_set_style_arc_width(ui_BatteryArc, 5, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(ui_BatteryArc, lv_color_hex(0x01070f), LV_PART_MAIN);
    lv_obj_set_style_arc_color(ui_BatteryArc, lv_color_hex(0x00FF00), LV_PART_INDICATOR); // Green color

    ui_BatteryLabel = lv_label_create(ui_MainScreen);
    lv_obj_set_style_text_font(ui_BatteryLabel, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);

        lv_obj_set_y(ui_BatteryLabel, 20);
        lv_obj_set_align(ui_BatteryLabel, LV_ALIGN_CENTER);
        lv_obj_set_style_text_color(ui_BatteryLabel, lv_color_hex(0x41C7FF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(ui_BatteryLabel, "?????");

        ui_WiFiLabel = lv_label_create(ui_MainScreen);
        lv_obj_set_width(ui_WiFiLabel, LV_SIZE_CONTENT);   /// 1
        lv_obj_set_height(ui_WiFiLabel, LV_SIZE_CONTENT);    /// 1
        lv_obj_set_x(ui_WiFiLabel, -35);
        lv_obj_set_y(ui_WiFiLabel, 50);
        lv_obj_set_align(ui_WiFiLabel, LV_ALIGN_CENTER);
        lv_label_set_text(ui_WiFiLabel, LV_SYMBOL_WIFI);
        //lv_obj_set_style_text_color(ui_WiFiLabel, lv_color_hex(0x41C7FF), LV_PART_MAIN | LV_STATE_DEFAULT);
        //Setting the WIFI label "off" colour
        lv_obj_set_style_text_color(ui_WiFiLabel, lv_color_hex(0x005578), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_opa(ui_WiFiLabel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(ui_WiFiLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(ui_WiFiLabel, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);

        ui_BTLabel = lv_label_create(ui_MainScreen);
        lv_obj_set_width(ui_BTLabel, LV_SIZE_CONTENT);   /// 1
        lv_obj_set_height(ui_BTLabel, LV_SIZE_CONTENT);    /// 1
        lv_obj_set_x(ui_BTLabel, 35);
        lv_obj_set_y(ui_BTLabel, 50);
        lv_obj_set_align(ui_BTLabel, LV_ALIGN_CENTER);
        lv_label_set_text(ui_BTLabel, LV_SYMBOL_BLUETOOTH);
        lv_obj_set_style_text_color(ui_BTLabel, lv_color_hex(0x41C7FF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_opa(ui_BTLabel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(ui_BTLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(ui_BTLabel, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);

        ui_screenselectlabel = lv_label_create(ui_MainScreen);
        lv_obj_set_width(ui_screenselectlabel, LV_SIZE_CONTENT);   /// 1
        lv_obj_set_height(ui_screenselectlabel, LV_SIZE_CONTENT);    /// 1
        lv_obj_set_align(ui_screenselectlabel, LV_ALIGN_CENTER);
        lv_label_set_text(ui_screenselectlabel, "Main");
        lv_obj_set_style_text_color(ui_screenselectlabel, lv_color_hex(0xFFAD00), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_opa(ui_screenselectlabel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

        ui_WeatherImage = lv_img_create(ui_MainScreen);
        lv_obj_set_width(ui_WeatherImage, 32);
        lv_obj_set_height(ui_WeatherImage, 32);
        lv_obj_set_x(ui_WeatherImage, -20);
        lv_obj_set_y(ui_WeatherImage, -50);
        lv_obj_set_align(ui_WeatherImage, LV_ALIGN_CENTER);
        lv_obj_add_flag(ui_WeatherImage, LV_OBJ_FLAG_ADV_HITTEST);     /// Flags
        lv_obj_clear_flag(ui_WeatherImage, LV_OBJ_FLAG_SCROLLABLE);      /// Flags

        ui_WeatherLabel = lv_label_create(ui_MainScreen);
        lv_obj_set_width(ui_WeatherLabel, LV_SIZE_CONTENT);   /// 1
        lv_obj_set_height(ui_WeatherLabel, LV_SIZE_CONTENT);    /// 1
        lv_obj_set_x(ui_WeatherLabel, 20);
        lv_obj_set_y(ui_WeatherLabel, -50);
        lv_obj_set_align(ui_WeatherLabel, LV_ALIGN_CENTER);
        lv_label_set_text(ui_WeatherLabel, "?"); // needs to have the symbol °
        lv_obj_set_style_text_color(ui_WeatherLabel, lv_color_hex(0x41C7FF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_opa(ui_WeatherLabel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(ui_WeatherLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(ui_WeatherLabel, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_add_event_cb(ui_MainArcMenu, ui_event_MainArcMenu, LV_EVENT_ALL, NULL);

        // NEW STUFF: CLOCK

        // Create arcs for seconds, minutes, and hours
        second_arc = lv_arc_create(ui_MainScreen);
        lv_obj_set_size(second_arc, 450, 450);
        lv_obj_center(second_arc);
        lv_arc_set_range(second_arc, 0, 60);
        lv_arc_set_rotation(second_arc, 270);
        lv_arc_set_bg_angles(second_arc, 0, 360);
        lv_arc_set_value(second_arc, 0);
        lv_obj_set_style_arc_opa(second_arc, 255, LV_PART_MAIN);
        lv_obj_set_style_arc_width(second_arc, 5, LV_PART_INDICATOR);
        lv_obj_set_style_arc_width(second_arc, 5, LV_PART_MAIN);
        lv_obj_set_style_arc_color(second_arc, lv_color_hex(0x0892fc), LV_PART_INDICATOR);
        lv_obj_remove_style(second_arc, NULL, LV_PART_KNOB); // Remove knob
        lv_obj_set_style_arc_color(second_arc, lv_color_hex(0x01070f), LV_PART_MAIN);
        lv_obj_clear_flag(second_arc, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_arc_rounded(second_arc, false, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_arc_rounded(second_arc, false, LV_PART_INDICATOR | LV_STATE_DEFAULT);

        minute_arc = lv_arc_create(ui_MainScreen);
        lv_obj_set_size(minute_arc, 430, 430);
        lv_obj_center(minute_arc);
        lv_arc_set_range(minute_arc, 0, 60);
        lv_arc_set_rotation(minute_arc, 270);
        lv_arc_set_bg_angles(minute_arc, 0, 360);
        lv_arc_set_value(minute_arc, 0);
        lv_obj_set_style_arc_opa(minute_arc, 255, LV_PART_MAIN);
        lv_obj_set_style_arc_width(minute_arc, 5, LV_PART_INDICATOR);
        lv_obj_set_style_arc_width(minute_arc, 5, LV_PART_MAIN);
        lv_obj_set_style_arc_color(minute_arc, lv_color_hex(0x8800ff), LV_PART_INDICATOR);
        lv_obj_remove_style(minute_arc, NULL, LV_PART_KNOB); // Remove knob
        lv_obj_set_style_arc_color(minute_arc, lv_color_hex(0x01070f), LV_PART_MAIN);
        lv_obj_clear_flag(minute_arc, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_arc_rounded(minute_arc, false, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_arc_rounded(minute_arc, false, LV_PART_INDICATOR | LV_STATE_DEFAULT);

        hour_arc = lv_arc_create(ui_MainScreen);
        lv_obj_set_size(hour_arc, 410, 410);
        lv_obj_center(hour_arc);
        lv_arc_set_range(hour_arc, 0, 12);
        lv_arc_set_rotation(hour_arc, 270);
        lv_arc_set_bg_angles(hour_arc, 0, 360);
        lv_arc_set_value(hour_arc, 0);
        lv_obj_set_style_arc_opa(hour_arc, 255, LV_PART_MAIN);
        lv_obj_set_style_arc_width(hour_arc, 5, LV_PART_INDICATOR);
        lv_obj_set_style_arc_width(hour_arc, 5, LV_PART_MAIN);
        lv_obj_set_style_arc_color(hour_arc, lv_color_hex(0xcb2eff), LV_PART_INDICATOR);
        lv_obj_remove_style(hour_arc, NULL, LV_PART_KNOB); // Remove knob
        lv_obj_set_style_arc_color(hour_arc, lv_color_hex(0x01070f), LV_PART_MAIN);
        lv_obj_clear_flag(hour_arc, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_arc_rounded(hour_arc, false, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_arc_rounded(hour_arc, false, LV_PART_INDICATOR | LV_STATE_DEFAULT);

        // --- Mid info ring (between outer clock scale and inner arcs) ---
        // Visual: a single subtle/dark blue circle.
        // Content: date around ~1-2 o'clock, digital time around ~10 o'clock.
        ui_MidInfoRing = lv_arc_create(ui_MainScreen);
        lv_obj_set_size(ui_MidInfoRing, 250, 250);
        lv_obj_center(ui_MidInfoRing);
        lv_arc_set_bg_angles(ui_MidInfoRing, 0, 360);
        lv_arc_set_rotation(ui_MidInfoRing, 270);
        lv_arc_set_value(ui_MidInfoRing, 0);
        lv_obj_remove_style(ui_MidInfoRing, NULL, LV_PART_KNOB);
        lv_obj_clear_flag(ui_MidInfoRing, LV_OBJ_FLAG_CLICKABLE);

        // Ring line style
        lv_obj_set_style_arc_width(ui_MidInfoRing, 2, LV_PART_MAIN);
        lv_obj_set_style_arc_color(ui_MidInfoRing, lv_color_hex(0x103357), LV_PART_MAIN); // dark-ish blue
        lv_obj_set_style_arc_opa(ui_MidInfoRing, 200, LV_PART_MAIN);

        // Hide indicator completely
        lv_obj_set_style_arc_width(ui_MidInfoRing, 0, LV_PART_INDICATOR);
        lv_obj_set_style_arc_opa(ui_MidInfoRing, 0, LV_PART_INDICATOR);

        // --- Date arc label ---
        ui_DateArcLabel = lv_arclabel_create(ui_MainScreen);
        lv_obj_set_size(ui_DateArcLabel, 300, 300);
        lv_obj_center(ui_DateArcLabel);
        lv_obj_clear_flag(ui_DateArcLabel, LV_OBJ_FLAG_CLICKABLE);
        lv_arclabel_set_radius(ui_DateArcLabel, 130);
        lv_arclabel_set_angle_start(ui_DateArcLabel, 290); // ~1 o'clock
        lv_arclabel_set_angle_size(ui_DateArcLabel, 150);
        lv_arclabel_set_dir(ui_DateArcLabel, LV_ARCLABEL_DIR_CLOCKWISE);
        lv_arclabel_set_text_vertical_align(ui_DateArcLabel, LV_ARCLABEL_TEXT_ALIGN_TRAILING);
        lv_arclabel_set_text_horizontal_align(ui_DateArcLabel, LV_ARCLABEL_TEXT_ALIGN_LEADING);
        lv_obj_set_style_text_font(ui_DateArcLabel, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(ui_DateArcLabel, lv_color_hex(0x2A5D8F), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_opa(ui_DateArcLabel, 220, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_arclabel_set_text(ui_DateArcLabel, "--- -- -- ----");

        // --- Digital time arc label ---
        ui_TimeArcLabel = lv_arclabel_create(ui_MainScreen);
        lv_obj_set_size(ui_TimeArcLabel, 300, 300);
        lv_obj_center(ui_TimeArcLabel);
        lv_obj_clear_flag(ui_TimeArcLabel, LV_OBJ_FLAG_CLICKABLE);
        lv_arclabel_set_radius(ui_TimeArcLabel, 130);
        lv_arclabel_set_angle_start(ui_TimeArcLabel, 150); // ~10 o'clock-ish
        lv_arclabel_set_angle_size(ui_TimeArcLabel, 120);
        lv_arclabel_set_dir(ui_TimeArcLabel, LV_ARCLABEL_DIR_CLOCKWISE);
        lv_arclabel_set_text_vertical_align(ui_TimeArcLabel, LV_ARCLABEL_TEXT_ALIGN_TRAILING);
        lv_arclabel_set_text_horizontal_align(ui_TimeArcLabel, LV_ARCLABEL_TEXT_ALIGN_CENTER);
        lv_obj_set_style_text_font(ui_TimeArcLabel, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(ui_TimeArcLabel, lv_color_hex(0x2A5D8F), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_opa(ui_TimeArcLabel, 220, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_arclabel_set_text(ui_TimeArcLabel, "--:--:--");

        create_combined_scale();
        
        // Call update to set initial values
        update_main_screen();
    
}

void create_segmented_ring(lv_obj_t * parent) {
    /* Allocate buffer in PSRAM */
    //lv_color_t * canvas_buf = (lv_color_t *)heap_caps_malloc(CANVAS_WIDTH * CANVAS_HEIGHT * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    static uint8_t * canvas_buf = (uint8_t *)heap_caps_malloc(CANVAS_WIDTH * CANVAS_HEIGHT * sizeof(LV_COLOR_FORMAT_ARGB8888), MALLOC_CAP_SPIRAM);
    
    if(canvas_buf == NULL) {
        // Handle allocation failure
        printf("Failed to allocate canvas buffer in PSRAM\n");
        return;
    }
 /* Create the canvas object */
    lv_obj_t * canvas = lv_canvas_create(parent);

    /* Set the drawing buffer for the canvas */
   // lv_canvas_set_draw_buf(canvas, canvas_buf);
   lv_canvas_set_buffer(canvas, canvas_buf, CANVAS_WIDTH, CANVAS_HEIGHT, LV_COLOR_FORMAT_ARGB8888);

    /* Set the canvas size */
    lv_obj_set_size(canvas, CANVAS_WIDTH, CANVAS_HEIGHT);

    /* Fill the canvas background (optional) */
  //  lv_canvas_fill_bg(canvas, lv_color_hex3(0x000), 255);

    /* Center the canvas */
    lv_obj_center(canvas);

    /* Initialize the drawing layer */
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);

    /* Define segment parameters */
    uint16_t num_segments = 12; // Number of segments
    uint16_t gap_degree = 15;    // Gap between segments in degrees

    /* Calculate the angle covered by each segment */
    uint16_t total_gap = gap_degree * num_segments;
    uint16_t segment_angle = (360 - total_gap) / num_segments;

    /* Arc descriptor */
    lv_draw_arc_dsc_t arc_dsc;
    lv_draw_arc_dsc_init(&arc_dsc);
    //arc_dsc.color = lv_color_hex(0x41C7FF); // Pale cyan color
    
    arc_dsc.color = lv_color_hex(0x0889bf); //darker cyan color
    arc_dsc.width = 10;                     // Thickness of segments
    arc_dsc.rounded = 0;                    // Set to 1 for rounded ends
    arc_dsc.opa = LV_OPA_COVER;             // Opacity

    /* Set center and radius */
    arc_dsc.center.x = CANVAS_WIDTH / 2;
    arc_dsc.center.y = CANVAS_HEIGHT / 2;
    arc_dsc.radius = (CANVAS_WIDTH < CANVAS_HEIGHT ? CANVAS_WIDTH : CANVAS_HEIGHT) / 2 - arc_dsc.width / 2;

    /* Start drawing each segment */
    uint16_t start_angle = 0;
    for(uint16_t i = 0; i < num_segments; i++)
    {
        uint16_t end_angle = start_angle + segment_angle;

        /* Set the start and end angles (in 0.1 degrees) */
        arc_dsc.start_angle = start_angle;
        arc_dsc.end_angle = end_angle;

        /* Draw the arc (segment) */
        lv_draw_arc(&layer, &arc_dsc);

        /* Increment start angle by segment angle and gap */
        start_angle = end_angle + gap_degree;
    }

    /* Finish drawing on the layer */
    lv_canvas_finish_layer(canvas, &layer);

    // No need to free canvas_buf here; it will be freed when the canvas is deleted

}

void create_combined_scale(void) {
    lv_obj_t * MainClockScale = lv_scale_create(ui_MainScreen);
    lv_obj_clear_flag(MainClockScale, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_blend_mode(MainClockScale, LV_BLEND_MODE_MULTIPLY, LV_PART_ANY);
    // Set the size to cover the outer ring
    lv_obj_set_size(MainClockScale, 466, 466);
    lv_obj_center(MainClockScale);

    // Configure the scale
    lv_scale_set_mode(MainClockScale, LV_SCALE_MODE_ROUND_INNER);
    lv_scale_set_rotation(MainClockScale, 270); // Start at the top
    lv_scale_set_angle_range(MainClockScale, 360);

    // Set the number of ticks
    lv_scale_set_total_tick_count(MainClockScale, 60 + 1); // +1 to include the last tick

    // Set major tick every 5 ticks (12 major ticks for hours)
    lv_scale_set_major_tick_every(MainClockScale, 5);

    // Hide labels
    lv_scale_set_label_show(MainClockScale, false);

     lv_obj_set_style_arc_width(MainClockScale, 2, LV_PART_MAIN);
    lv_obj_set_style_arc_color(MainClockScale, lv_color_hex(0x41C7FF), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(MainClockScale, 100, LV_PART_MAIN);

    // Set the radius to match your outermost arc
   // lv_obj_set_style_radius(MainClockScale, SCALE_RADIUS, 0);

     // Style for minor ticks (minute/second markers)
    lv_obj_set_style_line_width(MainClockScale, 1, LV_PART_ITEMS); // Thickness
    //lv_obj_set_style_scale_end_line_width(MainClockScale, 1, LV_PART_ITEMS); // For symmetry
    lv_obj_set_style_length(MainClockScale, 10, LV_PART_ITEMS); // Length
   // lv_obj_set_style_line_color(MainClockScale, lv_color_hex(0x19515e), LV_PART_ITEMS); // Color
    lv_obj_set_style_line_color(MainClockScale, lv_color_hex(0x41C7FF), LV_PART_ITEMS); // Color
    lv_obj_set_style_arc_opa(MainClockScale, 100, LV_PART_ITEMS);

    // Style for major ticks (hour markers)
    lv_obj_set_style_line_width(MainClockScale, 5, LV_PART_INDICATOR); // Thickness
    //lv_obj_set_style_scale_end_line_width(MainClockScale, 3, LV_PART_TICKS); // For symmetry
    lv_obj_set_style_length(MainClockScale, 10, LV_PART_INDICATOR); // Length
    lv_obj_set_style_line_color(MainClockScale, lv_color_hex(0x41C7FF), LV_PART_INDICATOR); // Color

    // Adjust colors to match your arcs if needed
    // For example, use lv_color_hex(0x41C7FF) or other colors


}

void update_main_screen(void) {
    if (!ui_MainScreen || !lv_obj_is_valid(ui_MainScreen)) {
        // Screen not initialized or not valid
        return;
    }

    // Update date + digital time (system time)
    if (ui_DateArcLabel && ui_TimeArcLabel) {
        time_t now = time(NULL);
        struct tm t;
        localtime_r(&now, &t);

        // Persistent buffers (avoid heap churn)
        static char date_buf[24]; // e.g. "Tue 06-01-2026"
        static char time_buf[16]; // e.g. "00:58:12"

        strftime(date_buf, sizeof(date_buf), "%a %d-%m-%Y", &t);
        strftime(time_buf, sizeof(time_buf), "%H:%M:%S", &t);

        lv_arclabel_set_text_static(ui_DateArcLabel, date_buf);
        lv_arclabel_set_text_static(ui_TimeArcLabel, time_buf);
    }

    // Update arc values
    lv_arc_set_value(second_arc, second_value);
    lv_arc_set_value(minute_arc, minute_value);
    lv_arc_set_value(hour_arc, hour_value % 12); // 0 to 11
  
}

void ui_mainscreen_apply_weather(uint16_t id, const char* tempText)
{
    if (ui_WeatherImage) {
        const char* iconPath = getMeteoconIcon(id, true);
        Serial.println("Icon path set in MainScreen UI");
        lv_img_set_src(ui_WeatherImage, iconPath);

      //  lv_color_t sci_fi_blue = lv_color_make(0, 200, 255);
      //  lv_obj_set_style_img_recolor(ui_WeatherImage, sci_fi_blue, LV_PART_MAIN);
      //  lv_obj_set_style_img_recolor_opa(ui_WeatherImage, LV_OPA_90, LV_PART_MAIN);
    }

    if (ui_WeatherLabel && tempText) {
        lv_label_set_text(ui_WeatherLabel, tempText);
    }
}1