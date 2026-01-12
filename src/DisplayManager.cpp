#include "DisplayManager.h"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>  // brings in Arduino_GFX + Arduino_CO5300

DisplayManager& DisplayManager::instance()
{
  static DisplayManager inst;
  return inst;
}

void DisplayManager::begin(Arduino_GFX* gfx)
{
  gfx_ = gfx;

  // Don’t call begin() on gfx here; main.cpp owns the bring-up order.
  // But we can safely push cached state if gfx already began.
  // If you call setBrightness/setScreenOn before begin(), it will be cached.
  applyScreenOn_(screenOn_);
  applyBrightness_(brightness_);
}

bool DisplayManager::isReady() const
{
  return gfx_ != nullptr;
}

void DisplayManager::setBrightness(uint8_t value)
{
  brightness_ = value;
  fading_ = false; // explicit set cancels fade
  applyBrightness_(brightness_);
}

uint8_t DisplayManager::getBrightness() const
{
  return brightness_;
}

void DisplayManager::fadeTo(uint8_t target, uint16_t durationMs)
{
  fadeStart_ = brightness_;
  fadeTarget_ = target;
  fadeDurationMs_ = (durationMs == 0) ? 1 : durationMs;
  fadeStartMs_ = millis();
  fading_ = true;
}

bool DisplayManager::isFading() const
{
  return fading_;
}

void DisplayManager::setScreenOn(bool on)
{
  screenOn_ = on;
  applyScreenOn_(screenOn_);
}

bool DisplayManager::isScreenOn() const
{
  return screenOn_;
}

void DisplayManager::tick()
{
  if(!fading_) return;

  const uint32_t now = millis();
  const uint32_t elapsed = now - fadeStartMs_;

  if(elapsed >= fadeDurationMs_) {
    brightness_ = fadeTarget_;
    fading_ = false;
    applyBrightness_(brightness_);
    return;
  }

  // Linear interpolation
  const float t = (float)elapsed / (float)fadeDurationMs_;
  const int delta = (int)fadeTarget_ - (int)fadeStart_;
  const uint8_t v = (uint8_t)((int)fadeStart_ + (int)(delta * t));

  // Only push if changed to avoid spamming bus
  if(v != brightness_) {
    brightness_ = v;
    applyBrightness_(brightness_);
  }
}

void DisplayManager::applyBrightness_(uint8_t value)
{
  if(!gfx_) return;

  // Only meaningful when screen is on
  if(!screenOn_) return;

  // We *know* this board is Arduino_CO5300.
  // If you ever swap panels, this is the only place to change.
  auto* co = static_cast<Arduino_CO5300*>(gfx_);
  co->setBrightness(value);
}

void DisplayManager::applyScreenOn_(bool on)
{
  if(!gfx_) return;

  // Arduino_CO5300 implements displayOn/Off on the driver
  auto* co = static_cast<Arduino_CO5300*>(gfx_);
  if(on) co->displayOn();
  else   co->displayOff();
}
