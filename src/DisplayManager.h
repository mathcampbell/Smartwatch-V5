#pragma once

#include <stdint.h>

class Arduino_GFX;   // forward-declare (keeps includes light)

// DisplayManager owns "display-level" controls that are not LVGL UI logic:
// - brightness (CO5300 register write)
// - screen on/off
// - optional fading (non-blocking)
// It does NOT own PMU/power logic.
class DisplayManager
{
public:
  static DisplayManager& instance();

  // Call once after you construct gfx (before you need brightness control).
  // gfx must actually be an Arduino_CO5300 instance on this board.
  void begin(Arduino_GFX* gfx);

  bool isReady() const;

  // Brightness 0..255 (CO5300)
  void setBrightness(uint8_t value);
  uint8_t getBrightness() const;

  // Optional: fades to target over time without blocking.
  // Call tick() from loop.
  void fadeTo(uint8_t target, uint16_t durationMs);
  bool isFading() const;

  // Panel power state (CO5300 supports displayOn/Off)
  void setScreenOn(bool on);
  bool isScreenOn() const;

  // Call regularly (e.g. every loop) if you use fadeTo()
  void tick();

private:
  DisplayManager() = default;

  // Implementation detail: we keep Arduino_GFX* but cast internally to CO5300
  Arduino_GFX* gfx_ = nullptr;

  // state cache
  bool screenOn_ = true;
  uint8_t brightness_ = 255;

  // fade state
  bool fading_ = false;
  uint8_t fadeStart_ = 255;
  uint8_t fadeTarget_ = 255;
  uint32_t fadeStartMs_ = 0;
  uint16_t fadeDurationMs_ = 0;

  // helpers
  void applyBrightness_(uint8_t value);
  void applyScreenOn_(bool on);
};
