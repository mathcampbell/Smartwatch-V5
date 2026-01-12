#include "uiWeatherScreen.h"
#include "WeatherManager.h"
#include <Arduino.h>

// ---- Screen constants ----
static constexpr int16_t W = 466;
static constexpr int16_t H = 466;

// ---- LVGL objects ----
lv_obj_t * ui_WeatherScreen = nullptr;

static lv_obj_t * s_bg      = nullptr;   // background image
static lv_obj_t * s_fxLayer = nullptr;   // overlay we custom-draw
static lv_obj_t * s_lblTemp = nullptr;
static lv_obj_t * s_lblCond = nullptr;

// ---- Change detection ----
static uint16_t      s_lastId = 0xFFFF;
static unsigned long s_lastDt = 0;
static String        s_lastIcon;

// ---- FX state ----
enum Fx : uint8_t { FX_NONE, FX_RAIN_LIGHT, FX_RAIN_HEAVY, FX_SNOW, FX_SLEET, FX_FOG, FX_THUNDER };
static Fx s_fx = FX_NONE;
static lv_timer_t* s_fxTimer = nullptr;

// Rain drops
struct Drop { int16_t x,y; uint8_t len,speed,opa; int8_t drift; uint8_t w; };
static constexpr int MAX_DROPS = 170;
static Drop s_drops[MAX_DROPS];
static int  s_dropCount = 0;

// Snow/hail particles
struct Part { int16_t x,y; int8_t vx; uint8_t r,speed,opa; bool hard; };
static constexpr int MAX_PARTS = 120;
static Part s_parts[MAX_PARTS];
static int  s_partCount = 0;

// Thunder flash
static uint8_t s_flashFrames = 0;
static uint8_t s_flashOpa = 0;

// Fog phase
static int16_t s_fogPhase = 0;

// ---- Forward decls ----
static const char* pick_bg(const WeatherData& wd);
static Fx pick_fx(uint16_t id);
static void fx_start(Fx fx);
static void fx_stop(void);

static void init_rain(bool heavy, bool sleet);
static void init_snow(bool hail);
static void init_fog(void);
static void init_thunder(void);

static void step_rain(void);
static void step_snow(void);
static void step_fog(void);
static void step_thunder(void);

static void fx_tick_cb(lv_timer_t* t);
static void fx_draw_cb(lv_event_t* e);
static void draw_fx(lv_draw_ctx_t* draw_ctx, const lv_area_t* a);

void ui_WeatherScreen_screen_init(void)
{
    if (ui_WeatherScreen) return;

    // Seed RNG once
    static bool seeded = false;
    if (!seeded) { randomSeed((uint32_t)esp_random()); seeded = true; }

    ui_WeatherScreen = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_WeatherScreen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(ui_WeatherScreen, W, H);

    // Background (sky-only)
    s_bg = lv_img_create(ui_WeatherScreen);
    lv_obj_set_size(s_bg, W, H);
    lv_obj_set_pos(s_bg, 0, 0);
    lv_img_set_src(s_bg, "A:/lvgl/weather/heavy_clouds.png");

    // FX layer
    s_fxLayer = lv_obj_create(ui_WeatherScreen);
    lv_obj_remove_style_all(s_fxLayer);
    lv_obj_set_size(s_fxLayer, W, H);
    lv_obj_set_pos(s_fxLayer, 0, 0);
    lv_obj_clear_flag(s_fxLayer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_fxLayer, fx_draw_cb, LV_EVENT_DRAW_MAIN, NULL);

    // Minimal overlay text (you'll replace with your real layout)
    s_lblTemp = lv_label_create(ui_WeatherScreen);
    lv_obj_set_style_text_color(s_lblTemp, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(s_lblTemp, LV_ALIGN_TOP_MID, 0, 40);
    lv_label_set_text(s_lblTemp, "--°");

    s_lblCond = lv_label_create(ui_WeatherScreen);
    lv_obj_set_style_text_color(s_lblCond, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(s_lblCond, LV_ALIGN_TOP_MID, 0, 72);
    lv_label_set_text(s_lblCond, "Weather");
}

void ui_WeatherScreen_tick(void)
{
    if (!ui_WeatherScreen) return;
    if (lv_scr_act() != ui_WeatherScreen) return; // only when active

    const WeatherData& wd = WeatherGet(); // real getter :contentReference[oaicite:4]{index=4}

    const bool changed =
        (wd.id != s_lastId) ||
        (wd.dt != s_lastDt) ||
        (wd.icon != s_lastIcon);

    if (!changed) return;

    s_lastId = wd.id;
    s_lastDt = wd.dt;
    s_lastIcon = wd.icon;

    // Update overlay text
    if (s_lblTemp) lv_label_set_text(s_lblTemp, wd.temperature.c_str());
    if (s_lblCond) lv_label_set_text(s_lblCond, wd.condition.c_str());

    // Update background
    if (s_bg) lv_img_set_src(s_bg, pick_bg(wd));

    // Update FX
    Fx want = pick_fx(wd.id);
    if (want != s_fx) fx_start(want);
}

// ---------------- Background mapping ----------------
// Uses wd.icon last char (d/n) for day/night, because it's reliable in your struct :contentReference[oaicite:5]{index=5}
static const char* pick_bg(const WeatherData& wd)
{
    const uint16_t id = wd.id;
    const bool night = (wd.icon.length() >= 3 && wd.icon.charAt(2) == 'n');

    if (id == 800) return night ? "A:/lvgl/weather/clear-night-bg.png" : "A:/lvgl/weather/clear-day-bg.png";
    if (id == 801) return night ? "A:/lvgl/weather/partly-cloudy-night-bg.png" : "A:/lvgl/weather/partly-cloudy-day-bg.png";
    if (id == 802 || id == 803 || id == 804) return "A:/lvgl/weather/heavy-clouds-bg.png";

    if (id / 100 == 2) return "A:/lvgl/weather/thunderstorm-bg.png";
    if (id / 100 == 3) return "A:/lvgl/weather/drizzle-bg.png";
    if (id / 100 == 5) return (id == 500) ? "A:/lvgl/weather/light-rain-bg.png" : "A:/lvgl/weather/heavy-rain-bg.png";
    if (id / 100 == 6) return (id >= 611 && id <= 616) ? "A:/lvgl/weather/sleet-bg.png" : "A:/lvgl/weather/snow-bg.png";
    if (id / 100 == 7) return "A:/lvgl/weather/fog-bg.png";

    return "A:/lvgl/weather/heavy-clouds-bg.png";
}

static Fx pick_fx(uint16_t id)
{
    if (id / 100 == 2) return FX_THUNDER;

    // Drizzle: background already communicates “wet air / wet lens” -> keep FX off
    if (id / 100 == 3) return FX_NONE;

    if (id == 500)     return FX_RAIN_LIGHT;
    if (id / 100 == 5) return FX_RAIN_HEAVY;

    if (id == 511) return FX_SLEET;
    if (id >= 611 && id <= 616) return FX_SLEET;

    if (id / 100 == 6) return FX_SNOW;
    if (id / 100 == 7) return FX_FOG;

    return FX_NONE;
}

// ---------------- FX engine ----------------
static void fx_start(Fx fx)
{
    if (s_fx == fx) return;
    fx_stop();
    s_fx = fx;

    switch (s_fx) {
        case FX_RAIN_LIGHT: init_rain(false,false); break;
        case FX_RAIN_HEAVY: init_rain(true,false);  break;
        case FX_SNOW:       init_snow(false);       break;
        case FX_SLEET:      init_rain(true,true); init_snow(true); break;
        case FX_FOG:        init_fog();             break;
        case FX_THUNDER:    init_thunder(); init_rain(true,false); break;
        default: break;
    }

    if (s_fx != FX_NONE) s_fxTimer = lv_timer_create(fx_tick_cb, 33, NULL); // ~30fps
    lv_obj_invalidate(s_fxLayer);
}

static void fx_stop(void)
{
    if (s_fxTimer) { lv_timer_del(s_fxTimer); s_fxTimer = nullptr; }
    s_fx = FX_NONE;
    s_dropCount = 0;
    s_partCount = 0;
    s_flashFrames = 0;
    s_flashOpa = 0;
    s_fogPhase = 0;
    if (s_fxLayer) lv_obj_invalidate(s_fxLayer);
}

static void init_rain(bool heavy, bool sleet)
{
    s_dropCount = heavy ? 150 : 85;
    if (s_dropCount > MAX_DROPS) s_dropCount = MAX_DROPS;

    for (int i=0;i<s_dropCount;i++) {
        s_drops[i].x = (int16_t)random(0, W);
        s_drops[i].y = (int16_t)random(-H, H);
        s_drops[i].len   = (uint8_t)random(10, heavy ? 34 : 22);
        s_drops[i].speed = (uint8_t)random(heavy ? 10 : 6, heavy ? 20 : 12);
        s_drops[i].opa   = (uint8_t)random(sleet ? 100 : 60, sleet ? 220 : 160);
        s_drops[i].drift = (int8_t)random(sleet ? -3 : -1, sleet ? 4 : 2);
        const bool thick = (heavy && (random(0,10)==0)) || (sleet && (random(0,6)==0));
        s_drops[i].w = (uint8_t)(thick ? 2 : 1);
    }
}

static void init_snow(bool hail)
{
    s_partCount = hail ? 85 : 95;
    if (s_partCount > MAX_PARTS) s_partCount = MAX_PARTS;

    for (int i=0;i<s_partCount;i++) {
        s_parts[i].x = (int16_t)random(0, W);
        s_parts[i].y = (int16_t)random(-H, H);
        s_parts[i].hard = hail;
        s_parts[i].r = (uint8_t)random(hail ? 1 : 2, hail ? 3 : 5);
        s_parts[i].speed = (uint8_t)random(hail ? 8 : 2, hail ? 14 : 7);
        s_parts[i].vx = (int8_t)random(-2,3);
        s_parts[i].opa = (uint8_t)random(hail ? 120 : 70, hail ? 230 : 170);
    }
}

static void init_fog(void)      { s_fogPhase = 0; }
static void init_thunder(void)  { s_flashFrames = 0; s_flashOpa = 0; }

static void step_rain(void)
{
    for (int i=0;i<s_dropCount;i++) {
        s_drops[i].y += s_drops[i].speed;
        s_drops[i].x += s_drops[i].drift;
        if (s_drops[i].y > H+40 || s_drops[i].x < -40 || s_drops[i].x > W+40) {
            s_drops[i].y = -(int16_t)random(10,150);
            s_drops[i].x = (int16_t)random(0,W);
        }
    }
}

static void step_snow(void)
{
    for (int i=0;i<s_partCount;i++) {
        s_parts[i].y += s_parts[i].speed;
        s_parts[i].x += s_parts[i].vx;

        if (!s_parts[i].hard && (random(0,6)==0)) {
            int v = (int)s_parts[i].vx + (int)random(-1,2);
            if (v<-2) v=-2; if (v>2) v=2;
            s_parts[i].vx = (int8_t)v;
        }

        if (s_parts[i].y > H+12 || s_parts[i].x < -12 || s_parts[i].x > W+12) {
            s_parts[i].y = -(int16_t)random(5,170);
            s_parts[i].x = (int16_t)random(0,W);
        }
    }
}

static void step_fog(void) { s_fogPhase = (int16_t)(s_fogPhase + 1); }

static void step_thunder(void)
{
    if (s_flashFrames == 0 && random(0,80)==0) { s_flashFrames = 7; s_flashOpa = 220; }
    if (s_flashFrames) {
        s_flashFrames--;
        if (s_flashOpa > 45) s_flashOpa -= 35; else s_flashOpa = 0;
    }
}

static void fx_tick_cb(lv_timer_t* t)
{
    (void)t;
    switch (s_fx) {
        case FX_RAIN_LIGHT:
        case FX_RAIN_HEAVY:
        case FX_SLEET:   step_rain(); break;
        case FX_SNOW:    step_snow(); break;
        case FX_FOG:     step_fog();  break;
        case FX_THUNDER: step_thunder(); step_rain(); break;
        default: break;
    }
    lv_obj_invalidate(s_fxLayer);
}

static void fx_draw_cb(lv_event_t* e)
{
    lv_draw_ctx_t* draw_ctx = lv_event_get_draw_ctx(e);
    lv_obj_t* obj = lv_event_get_target(e);
    lv_area_t a;
    lv_obj_get_content_coords(obj, &a);
    draw_fx(draw_ctx, &a);
}

static void draw_fx(lv_draw_ctx_t* draw_ctx, const lv_area_t* a)
{
    // Rain / sleet / thunder lines
    if (s_fx==FX_RAIN_LIGHT || s_fx==FX_RAIN_HEAVY || s_fx==FX_SLEET || s_fx==FX_THUNDER) {
        lv_draw_line_dsc_t dsc;
        lv_draw_line_dsc_init(&dsc);
        dsc.round_start = 1;
        dsc.round_end = 1;
        dsc.color = (s_fx==FX_SLEET) ? lv_color_hex(0xEAF6FF) : lv_color_hex(0xBFDFFF);

        for (int i=0;i<s_dropCount;i++) {
            dsc.width = s_drops[i].w;
            dsc.opa   = s_drops[i].opa;
            lv_point_t p1 = { (int16_t)(a->x1 + s_drops[i].x), (int16_t)(a->y1 + s_drops[i].y) };
            lv_point_t p2 = { (int16_t)(p1.x + s_drops[i].drift), (int16_t)(p1.y + s_drops[i].len) };
            lv_draw_line(draw_ctx, &dsc, &p1, &p2);
        }
    }

    // Snow/hail particles
    if (s_fx==FX_SNOW || s_fx==FX_SLEET) {
        lv_draw_rect_dsc_t rd;
        lv_draw_rect_dsc_init(&rd);
        rd.border_width = 0;
        rd.radius = LV_RADIUS_CIRCLE;
        rd.bg_color = (s_fx==FX_SLEET) ? lv_color_hex(0xF4FBFF) : lv_color_white();

        for (int i=0;i<s_partCount;i++) {
            rd.bg_opa = s_parts[i].opa;
            int16_t x = (int16_t)(a->x1 + s_parts[i].x);
            int16_t y = (int16_t)(a->y1 + s_parts[i].y);
            int16_t r = (int16_t)(s_parts[i].r);
            lv_area_t rr = { (int16_t)(x-r), (int16_t)(y-r), (int16_t)(x+r), (int16_t)(y+r) };
            lv_draw_rect(draw_ctx, &rd, &rr);
        }
    }

    // Fog haze bands (cheap)
    if (s_fx==FX_FOG) {
        lv_draw_rect_dsc_t fd;
        lv_draw_rect_dsc_init(&fd);
        fd.border_width = 0;
        fd.radius = 0;
        fd.bg_color = lv_color_white();
        fd.bg_opa = 18;

        int16_t y0 = (int16_t)(a->y1 + (H/3) + (s_fogPhase % 40) - 20);
        int16_t y1 = (int16_t)(a->y1 + (H*2/3) - (s_fogPhase % 50) - 20);

        lv_area_t band1 = { a->x1, y0, a->x2, (int16_t)(y0 + 34) };
        lv_area_t band2 = { a->x1, y1, a->x2, (int16_t)(y1 + 38) };

        lv_draw_rect(draw_ctx, &fd, &band1);
        lv_draw_rect(draw_ctx, &fd, &band2);
    }

    // Thunder flash
    if (s_fx==FX_THUNDER && s_flashOpa) {
        lv_draw_rect_dsc_t flash;
        lv_draw_rect_dsc_init(&flash);
        flash.border_width = 0;
        flash.radius = 0;
        flash.bg_color = lv_color_white();
        flash.bg_opa = s_flashOpa;

        lv_area_t full = *a;
        lv_draw_rect(draw_ctx, &flash, &full);
    }
}
