#include <Arduino.h>
#include <Arduino_GFX_Library.h>  // Core graphics library
#include <time.h>
#include <Wire.h>  // For I2C communication with the I/O expander
#include <Adafruit_XCA9554.h>  // For the TCA9554 I/O expander
#define XPOWERS_CHIP_AXP2101
#include <XPowersLib.h> // For power management
#include "TouchDrvCSTXXX.hpp" // Touch driver for CSTXXX series
#include <lvgl.h>  // For LVGL library
#include "lv_conf.h"
#include "ui.h"
#include "clock.h"
#include "ui_events.h"
#include "DisplayManager.h"
#include "PowerManager.h"
#include "ui_Power.h"
#include "TimeManager.h"
#include "WeatherManager.h"
#include "WiFiManager.h"
#include <LittleFS.h>
#include "ui_MainScreen.h"
#include "uiWeatherScreen.h"
#include "SettingsManager.h"
#include "Tide.h"
#include "TideService.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"


//////////////////// DEFINITIONS ///////////////////////////////

#define I2C_SCL 10
#define I2C_SDA 11
#define TCA9554_ADDRESS 0x20  // I2C address for the IO expander

#define FORMAT_LITTLEFS_IF_FAILED true


// Define the pin connections for the touch panel
#define TOUCH_PIN 11 
#define TOUCH_RST 40
#define TOUCH_SCL 14
#define TOUCH_SDA 15


// Define the pin connections for the display
#define LCD_CS 12  // Chip select
#define LCD_SCK 38  // Serial clock
#define LCD_D0 4  // Data line 0
#define LCD_D1 5  // Data line 1
#define LCD_D2 6  // Data line 2
#define LCD_D3 7  // Data line 3
#define LCD_RST_PIN 39 // Reset pin

#define MCLKPIN             42
#define BCLKPIN              9
#define WSPIN               45
#define DOPIN               10
#define DIPIN                8
#define PA                  46

#define LCD_WIDTH 466
#define LCD_HEIGHT 466

#define LVGL_BUF_LEN (LCD_WIDTH * LCD_HEIGHT / 10)

#define DIRECT_MODE // Uncomment to enable full frame buffer

#define DRAW_BUF_SIZE (LCD_WIDTH * LCD_HEIGHT / 6 * (LV_COLOR_DEPTH / 8))
#define FULL_BUF_SIZE (LCD_WIDTH * LCD_HEIGHT * (LV_COLOR_DEPTH / 8))

// Change to suit your WiFi router
#define WIFI_SSID     "GraphicsForgeA"
#define WIFI_PASSWORD "25137916"

extern TideService g_tideService;

//Adding a seperate task to stop it crashing out
static TaskHandle_t lvglTaskHandle = nullptr;
static SemaphoreHandle_t lvglMutex = nullptr;

static inline void lvgl_lock()
{
  if(lvglMutex) xSemaphoreTake(lvglMutex, portMAX_DELAY);
}
static inline void lvgl_unlock()
{
  if(lvglMutex) xSemaphoreGive(lvglMutex);
}


// Create an instance of the Arduino_ESP32QSPI class
Arduino_DataBus *bus = new Arduino_ESP32QSPI(
  LCD_CS /* CS */, LCD_SCK /* SCK */, LCD_D0 /* SDIO0 */, LCD_D1 /* SDIO1 */,
  LCD_D2 /* SDIO2 */, LCD_D3 /* SDIO3 */);

Arduino_GFX *gfx = new Arduino_CO5300(
  bus,
  LCD_RST_PIN /* RST */,
  0 /* rotation */,
  LCD_WIDTH,
  LCD_HEIGHT,
  6 /* col_offset1 */,
  0 /* row_offset1 */,
  0 /* col_offset2 */,
  0 /* row_offset2 */
);


Adafruit_XCA9554 ioexp;
XPowersPMU power;

bool pmu_flag = false;
bool adc_switch = false;

lv_display_t *disp;
static lv_color_t *disp_draw_buf;
static lv_color_t *disp_draw_buf2;

uint32_t screenWidth;
uint32_t screenHeight;
uint32_t bufSize;

TouchDrvCSTXXX touch;
int16_t x[5], y[5];
bool isPressed = false;
unsigned long lastInteractionTime = 0;


// These will be filled from SettingsManager once FS/settings are loaded
static uint32_t SCREEN_DIM_TIMEOUT_MS = 30 * 1000;
static uint32_t SLEEP_AFTER_MS       = 60 * 1000;  // fallback defaults

bool isScreenDimmed = false;

// Brightness derived from currentSettings.brightness_level (0–100%)
static uint8_t g_fullBrightness = 255;   // what the user picked
static uint8_t g_dimBrightness  = 50;    // dimmed value



bool checkWeatherFlag = false;
static unsigned long last_weather_update = 0;

extern void ui_init();

extern void clock_init();


// put function declarations here:

void setFlag(void) {
  pmu_flag = true;
}

static void lvgl_task(void* pv)
{
  (void)pv;
  for(;;) {
    lvgl_lock();
    lv_timer_handler();           // all drawing + image decode happens here
    lvgl_unlock();

    vTaskDelay(pdMS_TO_TICKS(5)); // ~200 Hz; LVGL will internally pace redraws
  }
}

void notifyUserInteraction()
{
    lastInteractionTime = millis();

    if (isScreenDimmed) {
        DisplayManager::instance().setBrightness(g_fullBrightness);
        isScreenDimmed = false;
    }
}

/* // LVGL v9 flush callback
static void my_disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
 const int32_t w = lv_area_get_width(area);
 const int32_t h = lv_area_get_height(area);

 uint16_t * pixels = (uint16_t *)px_map;
  gfx->startWrite();
 #if (LV_COLOR_16_SWAP != 0)
  gfx->draw16bitBeRGBBitmap(area->x1, area->y1, pixels, w, h);
 #else
  gfx->draw16bitRGBBitmap(area->x1, area->y1, pixels, w, h);
  #endif
  gfx->endWrite();
  lv_display_flush_ready(disp);
} */ // OLD VERSION DO NOT USE

//////////////////////////////////////////


static void my_disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
  const int32_t w = lv_area_get_width(area);
  const int32_t h = lv_area_get_height(area);

  // Tune this. 16–32 lines is usually a sweet spot.
  constexpr int32_t CHUNK_LINES = 20;

  // Internal DMA bounce buffer (enough for full width × CHUNK_LINES)
  static uint16_t *dma_buf = nullptr;
  if(!dma_buf) {
    dma_buf = (uint16_t*)heap_caps_malloc(466 * CHUNK_LINES * sizeof(uint16_t),
                                         MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if(!dma_buf) {
      // If this fails, drop CHUNK_LINES or handle error.
      Serial.println("[LVGL] dma_buf alloc failed");
      lv_display_flush_ready(disp);
      return;
    }
  }

  const uint16_t *src = (const uint16_t*)px_map;

  gfx->startWrite();

  for(int32_t y = 0; y < h; y += CHUNK_LINES) {
    const int32_t lines = (y + CHUNK_LINES <= h) ? CHUNK_LINES : (h - y);
    const int32_t count = w * lines;

    // Copy into DMA-capable memory
    memcpy(dma_buf, src + (y * w), count * sizeof(uint16_t));


    gfx->draw16bitRGBBitmap(area->x1, area->y1 + y, dma_buf, w, lines);
  }

  gfx->endWrite();
  lv_display_flush_ready(disp);
}


uint32_t millis_cb(void)
{
  return millis();
}

static void rounder_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_INVALIDATE_AREA) {
        lv_area_t *area = (lv_area_t *)lv_event_get_param(e);
        if (area) {
            // Round coordinates for CO5300 display optimization
            area->x1 = (area->x1) & ~1; // Round down to even
            area->x2 = (area->x2) | 1;  // Round up to odd
            area->y1 = (area->y1) & ~1; // Round down to even  
            area->y2 = (area->y2) | 1;  // Round up to odd
        }
    }
}



void lvgl_init_display()
{
  lv_init();
  lv_tick_set_cb(millis_cb);

  screenWidth  = gfx->width();
  screenHeight = gfx->height();

// ---- LVGL draw buffer: SINGLE buffer, PARTIAL mode ----
// Start with 80 lines. If WiFi ever fails to init, drop to 60.
const uint32_t buf_lines = 160;  // try 160, then 200/240
const uint32_t buf_bytes = (uint32_t)screenWidth * buf_lines * sizeof(lv_color_t);

disp_draw_buf  = (lv_color_t*)heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
disp_draw_buf2 = NULL;

if(!disp_draw_buf) {
  Serial.println("[LVGL] disp_draw_buf alloc failed!");
  while(true) delay(100);
}

Serial.printf("[LVGL] internal DMA free after buffers: %u bytes\n",
              (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));

disp = lv_display_create(screenWidth, screenHeight);
lv_display_set_flush_cb(disp, my_disp_flush);

lv_display_set_buffers(disp,
                       disp_draw_buf,
                       NULL,
                       buf_bytes,
                       LV_DISPLAY_RENDER_MODE_PARTIAL);

Serial.println("[LVGL] display init done");
}




void init_touch()
{
  Serial.println("Now in init_touch");
  // If their library wants reset/int pins:
  touch.setPins(40, TOUCH_PIN);
  Serial.println("TouchSetPins done");
  bool ok = touch.begin(Wire, 0x5a, TOUCH_SDA, TOUCH_SCL);
  if(!ok) {
    Serial.println("Touch begin failed");
    while(true) delay(100);
  }
  Serial.println("Touch begin failed");

  touch.setMaxCoordinates(466, 466);
  touch.setMirrorXY(true, true);
  Serial.println("Touch coords set");
  pinMode(TOUCH_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(TOUCH_PIN),
                  [](){ isPressed = true; },
                  FALLING);
  Serial.println("Touch interrupt attached");
}

/*Read the touchpad*/
void my_touchpad_read(lv_indev_t *indev, lv_indev_data_t *data) {
  uint8_t touched = touch.getPoint(x, y, touch.getSupportTouchPoint());

  if (touched > 0) {
    data->state = LV_INDEV_STATE_PR;  
    data->point.x = x[0];             
    data->point.y = y[0];

    notifyUserInteraction();

  } else {
    data->state = LV_INDEV_STATE_REL;  
  }
}

void set_display_brightness(uint8_t level) {
  static_cast<Arduino_CO5300*>(gfx)->setBrightness(level);
}

int readPmuIrqFromExpander()
{
  return ioexp.digitalRead(5);
}

void update_battery_arc()
{
    const auto st = PowerManager::instance().state();

    const uint16_t battMv = st.battVoltageMv;                 // millivolts
    const float    voltage = battMv / 1000.0f;                // volts (debug only)
    const uint8_t  battery_percentage = st.batteryPercent;    // 0..100

    const bool usb_present = (st.externalPowerPresent || st.vbusGood);
    const bool charging    = st.charging;

    // --- No battery detected ---
    // Use PMU's explicit flag; keep the <2.0V guard as a sanity fallback.
    if (!st.batteryConnected || battMv < 2000) {
        lv_obj_set_style_arc_color(ui_BatteryArc, lv_color_hex(0xff0000), LV_PART_INDICATOR);
        lv_arc_set_angles(ui_BatteryArc, 0, 359);
        lv_label_set_text(ui_BatteryLabel, LV_SYMBOL_WARNING);
        lv_obj_set_style_text_color(ui_BatteryLabel, lv_color_hex(0xff0000),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
        return;
    }

    // Compute arc extent once
    const uint16_t end_angle = (uint16_t)((battery_percentage * 360u) / 100u);

    // --- Actively charging: blue arc at % + lightning icon ---
    if (usb_present && charging) {
        lv_arc_set_angles(ui_BatteryArc, 0, end_angle);
        lv_obj_set_style_arc_color(ui_BatteryArc, lv_color_hex(0x008deb), LV_PART_INDICATOR);

        lv_label_set_text(ui_BatteryLabel, LV_SYMBOL_CHARGE);
        lv_obj_set_style_text_color(ui_BatteryLabel, lv_color_hex(0x008deb),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
        return; // do not apply gradient / battery glyphs
    }

    // --- Not charging: arc is normal % ---
    lv_arc_set_angles(ui_BatteryArc, 0, end_angle);

    // Determine arc colour based on battery percentage (your gradient)
    lv_color_t arc_color;
    if (battery_percentage > 70) {
        // #00ffbb (100%) -> #5D64EB (70%)
        uint8_t factor = (uint8_t)((100 - battery_percentage) * 255 / 30);
        arc_color = lv_color_make(
            0x00 + (0x5D - 0x00) * factor / 255,
            0xFF + (0x64 - 0xFF) * factor / 255,
            0xBB + (0xEB - 0xBB) * factor / 255
        );
    } else if (battery_percentage > 30) {
        // #5D64EB (70%) -> #A343B8 (30%)
        uint8_t factor = (uint8_t)((70 - battery_percentage) * 255 / 40);
        arc_color = lv_color_make(
            0x5D + (0xA3 - 0x5D) * factor / 255,
            0x64 + (0x43 - 0x64) * factor / 255,
            0xEB + (0xB8 - 0xEB) * factor / 255
        );
    } else {
        // #A343B8 (30%) -> #C23D53 (0%)
        uint8_t factor = (uint8_t)((30 - battery_percentage) * 255 / 30);
        arc_color = lv_color_make(
            0xA3 + (0xC2 - 0xA3) * factor / 255,
            0x43 + (0x3D - 0x43) * factor / 255,
            0xB8 + (0x53 - 0xB8) * factor / 255
        );
    }

    lv_obj_set_style_arc_color(ui_BatteryArc, arc_color, LV_PART_INDICATOR);

    // --- Label icon selection ---
    if (usb_present && !charging) {
        // Plugged in but not charging: show USB icon
        lv_label_set_text(ui_BatteryLabel, LV_SYMBOL_USB);
        lv_obj_set_style_text_color(ui_BatteryLabel, lv_color_hex(0x008deb),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
        return; // keep USB icon; don't overwrite with battery glyphs
    }

    // Battery-only: show battery glyphs (your thresholds)
    if (battery_percentage >= 90) {
        lv_label_set_text(ui_BatteryLabel, LV_SYMBOL_BATTERY_FULL);
        lv_obj_set_style_text_color(ui_BatteryLabel, lv_color_hex(0x03fc4e),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
    } else if (battery_percentage >= 75) {
        lv_label_set_text(ui_BatteryLabel, LV_SYMBOL_BATTERY_3);
        lv_obj_set_style_text_color(ui_BatteryLabel, lv_color_hex(0x03fc4e),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
    } else if (battery_percentage >= 50) {
        lv_label_set_text(ui_BatteryLabel, LV_SYMBOL_BATTERY_2);
        lv_obj_set_style_text_color(ui_BatteryLabel, lv_color_hex(0x77fc03),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
    } else if (battery_percentage >= 20) {
        lv_label_set_text(ui_BatteryLabel, LV_SYMBOL_BATTERY_1);
        lv_obj_set_style_text_color(ui_BatteryLabel, lv_color_hex(0xfcf803),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
    } else {
        lv_label_set_text(ui_BatteryLabel, LV_SYMBOL_BATTERY_EMPTY);
        lv_obj_set_style_text_color(ui_BatteryLabel, lv_color_hex(0xff0000),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    // Optional debug:
    // Serial.printf("Batt %.3fV %u%% usb=%d chg=%d\n", voltage, battery_percentage, usb_present, charging);
}




void setup() {

   Serial.begin(115200);
    delay(200);
    Serial.println("BOOT on Serial (UART0)");
 
    Serial.println("Arduino_GFX SmartWatch V5 Starting...");


  String LVGL_Arduino = "Hello Arduino! ";
  //LVGL_Arduino += String('V') + lv_version_major() + "." + lv_version_minor() + "." + lv_version_patch();

  Wire.begin(I2C_SDA, I2C_SCL);

  if (!LittleFS.begin(false)) {
  Serial.println("[FS] LittleFS mount failed");
} else {
  Serial.println("[FS] LittleFS mounted");

 // Load or create settings.json → fills global currentSettings
    initializeSettingsData();

    Serial.println("[Settings] Loaded settings:");
    Serial.println("  wifi_ssd: " + currentSettings.wifi_ssd);
    Serial.println("  weather_lat: " + currentSettings.weather_lat);
    Serial.println("  weather_long: " + currentSettings.weather_long);

     // --- Apply user settings ---

  // Map settings.brightness_level (0–100) to 0–255 for the backlight
  uint16_t lvl = currentSettings.brightness_level;
  if (lvl > 100) lvl = 100;
  g_fullBrightness = (uint8_t)((lvl * 255UL) / 100UL);

  // Dimmed brightness: e.g. 25% of full, but not lower than 5
  g_dimBrightness = (uint8_t)(g_fullBrightness / 4);
  if (g_dimBrightness < 5) g_dimBrightness = 5;

  // Timeouts from settings (values stored in seconds)
  if (currentSettings.screen_dim_duration > 0) {
      SCREEN_DIM_TIMEOUT_MS = (uint32_t)currentSettings.screen_dim_duration * 1000UL;
  }
  if (currentSettings.sleep_duration > 0) {
      SLEEP_AFTER_MS = (uint32_t)currentSettings.sleep_duration * 1000UL;
  }

  // Apply the user brightness right away
  DisplayManager::instance().setBrightness(g_fullBrightness);
  }

  
  gfx->begin(30000000);
   // register with DisplayManager
  DisplayManager::instance().begin(gfx);

  // set a default brightness
  DisplayManager::instance().setBrightness(255);


  lvgl_init_display();
    Serial.println("LVGL_init_display ran");

  lv_display_add_event_cb(disp, rounder_event_cb, LV_EVENT_INVALIDATE_AREA, NULL);

  init_touch();
  Serial.println("Touch Initialized");

     /*Initialize the (dummy) input device driver*/
    lv_indev_t * indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER); /*Touchpad should have POINTER type*/
    lv_indev_set_read_cb(indev, my_touchpad_read);

    Serial.println("Input Driver set up");

  // --- IO Expander (Adafruit) ---
  if (!ioexp.begin(TCA9554_ADDRESS, &Wire)) {
    Serial.println("TCA9554 not found...");
    while (true) delay(50);
  }

  Serial.println("TCA9554 found...");

  // Match vendor example: pins 5 and 4 are inputs
  ioexp.pinMode(5, INPUT);
  ioexp.pinMode(4, INPUT);

  
  //adcOn(); // your existing function

  if (!PowerManager::instance().begin(Wire, AXP2101_SLAVE_ADDRESS, I2C_SDA, I2C_SCL)) {
  Serial.println("PMU is not online...");
  while (true) delay(50);
}

// IMPORTANT: your vendor logic implies "1 == asserted" for this board setup.
// If you later confirm it’s active-low, flip this to true.
PowerManager::instance().setIrqReadFn(readPmuIrqFromExpander, /*activeLow=*/true);
    clock_init();
    Serial.println("clock_init success");
    ui_init();
    Serial.println("ui_init success");

time_manager_begin();

time_manager_bootstrap_system_time_from_rtc();
 checkWeatherFlag = true;

   lvglMutex = xSemaphoreCreateMutex();
  if(!lvglMutex) {
    Serial.println("[LVGL] Failed to create mutex");
    while(true) delay(100);
  }

  // Big stack is the key: TJPGD decode + draw can be stack-hungry.
  // 16384 is usually enough; if you still see canary trips, go 20480.
  BaseType_t ok = xTaskCreatePinnedToCore(
      lvgl_task,
      "lvgl",
      16384,          // stack bytes
      nullptr,
      2,              // priority
      &lvglTaskHandle,
      1               // core 1 (Arduino loopTask often runs core 1 too; either is fine)
  );

  if(ok != pdPASS) {
    Serial.println("[LVGL] Failed to create lvgl task");
    while(true) delay(100);
  }

Serial.println("[LVGL] LVGL task started");
    
Serial.println("Setup finished");


  
}

void loop() {
  static bool weather_job_active = false;
  static uint32_t weather_job_started_ms = 0;
  static bool weather_ran_once = false;

  wifi_manager_tick();

  unsigned long currentTime = millis();

  if (!isScreenDimmed && millis() - lastInteractionTime > SCREEN_DIM_TIMEOUT_MS) {
  //  Serial.println("Dimming screen due to inactivity");
    DisplayManager::instance().fadeTo(50, 300);
    isScreenDimmed = true;
  }

  // If DisplayManager touches LVGL internally, keep it locked
  lvgl_lock();
  DisplayManager::instance().tick();
  lvgl_unlock();

PowerManager::instance().tick();

if (PowerManager::instance().consumePkeyShortPressed()) {
    // Go to light sleep; returns here after wake
    PowerManager::instance().enterLightSleep();

    // After wake:
    lastInteractionTime = millis();
    DisplayManager::instance().setBrightness(g_fullBrightness);

    // Force a full LVGL redraw so the clock hands etc. don't leave ghosts
    lvgl_lock();
    lv_obj_invalidate(lv_screen_active());   // LVGL 9: invalidate full active screen
    lvgl_unlock();
}

if (PowerManager::instance().consumePkeyLongPressed()) {
    lvgl_lock();
    lv_scr_load(ui_Power);
    lvgl_unlock();
}

if (millis() - lastInteractionTime > SLEEP_AFTER_MS) {
    // Inactivity-based sleep
    PowerManager::instance().enterLightSleep();

    // After wake:
    lastInteractionTime = millis();
    DisplayManager::instance().setBrightness(g_fullBrightness);

    lvgl_lock();
    lv_obj_invalidate(lv_screen_active());
    lvgl_unlock();
}

  // Battery UI update (likely LVGL)
  static uint32_t lastBattUiMs = 0;
  const uint32_t now = millis();
  if (now - lastBattUiMs >= 1000) {
    lastBattUiMs = now;
    lvgl_lock();
    update_battery_arc();
    lvgl_unlock();
  }

  // ---- WEATHER TRIGGER ----
  if (!weather_job_active && ((currentTime - last_weather_update >= 360000) || checkWeatherFlag)) {
    checkWeatherFlag = false;
    weather_job_active = true;
    weather_ran_once = false;
    weather_job_started_ms = millis();
    wifi_manager_start_connect(WIFI_SSID, WIFI_PASSWORD, 30000);
  }


  // ---- WIFI LABEL UI (LVGL!) ----
  lvgl_lock();
  switch (wifi_manager_state()) {
    case WIFI_MGR_CONNECTING: {
      const bool on = ((millis() / 400) % 2) == 0;
      lv_obj_set_style_text_color(ui_WiFiLabel,
          lv_color_hex(on ? 0x41C7FF : 0x005578),
          LV_PART_MAIN | LV_STATE_DEFAULT);
      break;
    }
    case WIFI_MGR_CONNECTED:
      lv_obj_set_style_text_color(ui_WiFiLabel, lv_color_hex(0x41C7FF), LV_PART_MAIN | LV_STATE_DEFAULT);
      break;
    default:
      lv_obj_set_style_text_color(ui_WiFiLabel, lv_color_hex(0x005578), LV_PART_MAIN | LV_STATE_DEFAULT);
      break;
  }
  lvgl_unlock();

  // ---- WEATHER JOB ----
  if (weather_job_active && wifi_manager_is_connected() && !weather_ran_once) {
    weather_ran_once = true;

    if (WeatherUpdate()) {
      const WeatherData& wd = WeatherGet();
      Serial.println("[Main] Applying weather to UI...");

      lvgl_lock();
      ui_mainscreen_apply_weather(wd.id, wd.temperature.c_str());
      lvgl_unlock();
    }

    time_t ntpEpoch;
    if (WeatherConsumeNtpSync(&ntpEpoch)) {
      time_t rtcEpoch;
      const bool rtcOk = time_manager_read_rtc_epoch(&rtcEpoch);
      const long tol = 5;
      if (!rtcOk || labs((long)(ntpEpoch - rtcEpoch)) > tol) {
        time_manager_write_rtc_from_system_time();
      }
    }

    wifi_manager_disconnect(true);
    last_weather_update = currentTime;
    weather_job_active = false;
  }

  if (weather_job_active && wifi_manager_state() == WIFI_MGR_FAILED) {
    weather_job_active = false;
    wifi_manager_disconnect(true);
  }

  // Weather screen tick touches LVGL (image + labels)
  lvgl_lock();
  ui_WeatherScreen_tick();
  lvgl_unlock();
}


