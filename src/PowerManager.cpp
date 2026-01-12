#include "PowerManager.h"

#include <Arduino.h>
#include <XPowersLib.h>   // from lewisxhe/XPowersLib
#include <esp_sleep.h>
#include <esp_system.h>
#include <driver/gpio.h>
#include "DisplayManager.h"

static constexpr gpio_num_t TP_INT_GPIO = GPIO_NUM_11;   // your TP_INT


PowerManager& PowerManager::instance()
{
    static PowerManager inst;
    return inst;
}

bool PowerManager::begin(TwoWire& wire, uint8_t axpAddress, int sda, int scl)
{
    wire_ = &wire;

    // Ensure I2C is started (you can also do this in main.cpp once)
    // Calling begin() twice with same pins is OK; changing pins is not.
    wire.begin(sda, scl);

    if(!pmu_) {
        pmu_ = new XPowersAXP2101();
        Serial.println("PowerManager: Created XPowersAXP2101 instance");
    }

    const bool ok = pmu_->begin(wire, axpAddress, sda, scl);
    if(!ok) return false;
    adcOn();

    // Basic PMU config (match your earlier example)
    pmu_->disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
    pmu_->setChargeTargetVoltage(3);
    pmu_->clearIrqStatus();
    Serial.println("PowerManager: PMU initialized");
    
    // We want down/up edges so we can measure hold time in software.
    pmu_->enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ); // press down
    pmu_->enableIRQ(XPOWERS_AXP2101_PKEY_LONG_IRQ); // release
    Serial.println("PowerManager: PMU IRQs configured");

    // Prime state immediately
    updateReadings_();
    st_.lastUpdateMs = millis();
    lastPollMs_ = st_.lastUpdateMs;

    return true;
}

void PowerManager::setPollIntervalMs(uint32_t ms)
{
    pollIntervalMs_ = (ms < 100) ? 100 : ms; // don’t spam I2C
}

void PowerManager::setPmuIrqGpio(int irqGpio, bool activeLow)
{
    pmuIrqGpio_ = irqGpio;
    pmuIrqActiveLow_ = activeLow;

    if(pmuIrqGpio_ >= 0) {
        pinMode(pmuIrqGpio_, activeLow ? INPUT_PULLUP : INPUT);
    }
}

PowerManager::PowerState PowerManager::state() const
{
    return st_;
}

bool PowerManager::isExternalPowerPresent() const
{
    return st_.externalPowerPresent;
}

bool PowerManager::isCharging() const
{
    return st_.charging;
}

uint8_t PowerManager::batteryPercent() const
{
    return st_.batteryPercent;
}

bool PowerManager::consumePkeyShortPressed()
{
    if(st_.pkeyShortPressed) {
        st_.pkeyShortPressed = false;
        return true;
    }
    return false;
}

bool PowerManager::consumePkeyLongPressed()
{
    if(st_.pkeyLongPressed) {
        st_.pkeyLongPressed = false;
        return true;
    }
    return false;
}

void PowerManager::setIrqReadFn(IrqReadFn fn, bool activeLow)
{
  irqReadFn_ = fn;
  irqActiveLow_ = activeLow;
}

void PowerManager::tick()
{
  const uint32_t now = millis();

  // If we have an IRQ reader, process when asserted
if (irqReadFn_) {
    bool asserted = irqReadFn_() ? true : false;
    if (irqActiveLow_) asserted = !asserted;

    // Drain: handle all pending PMU IRQ flags until line deasserts,
    // with a small safety limit to avoid infinite loop if something is wrong.
    for (int i = 0; i < 8 && asserted; i++) {
        updateIrq_();
        asserted = irqReadFn_() ? true : false;
        if (irqActiveLow_) asserted = !asserted;
    }
}

  if(now - lastPollMs_ >= pollIntervalMs_) {
    lastPollMs_ = now;
    updateReadings_();
    st_.lastUpdateMs = now;
  }

  // If we somehow missed the release edge, don't let keyDown_ persist forever.
// 6 seconds is safely > your 3s long-press threshold.
if (keyDown_ && (millis() - keyDownMs_ > 5000)) {
    keyDown_ = false;
}


}

void PowerManager::updateReadings_()
{
    if(!pmu_) return;

    // These calls match the XPowersLib methods you pasted from the example
    st_.chargerStatus = pmu_->getChargerStatus();

    st_.temperatureC = (int16_t)pmu_->getTemperature();

    st_.charging = pmu_->isCharging();
    st_.discharging = pmu_->isDischarge();
    st_.standby = pmu_->isStandby();

    st_.externalPowerPresent = pmu_->isVbusIn();
    st_.vbusGood = pmu_->isVbusGood();

    st_.battVoltageMv = (uint16_t)pmu_->getBattVoltage();
    st_.vbusVoltageMv = (uint16_t)pmu_->getVbusVoltage();
    st_.systemVoltageMv = (uint16_t)pmu_->getSystemVoltage();

    st_.batteryConnected = pmu_->isBatteryConnect();
    if(st_.batteryConnected) {
        st_.batteryPercent = (uint8_t)pmu_->getBatteryPercent();
    } else {
        st_.batteryPercent = 0;
    }
}

void PowerManager::updateIrq_()
{
    if (!pmu_) return;

    (void)pmu_->getIrqStatus();

    if (pmu_->isPekeyShortPressIrq()) {
        st_.pkeyShortPressed = true;
    }

    if (pmu_->isPekeyLongPressIrq()) {
        st_.pkeyLongPressed = true;
    }

    pmu_->clearIrqStatus();
}


void PowerManager::enterLightSleep()
{
  // 1) Quiesce your app peripherals here (keep it minimal)
  // - turn off backlight
  // - optionally pause LVGL tick / animations
  // (Use YOUR existing backlight method)
 // DisplayManager::instance().setBrightness(0);
  DisplayManager::instance().setScreenOn(false);
  // 2) Configure wake sources
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

  // Touch INT is almost always active-low; use pullup + wake on LOW
  
    pinMode((int)TP_INT_GPIO, INPUT_PULLUP);
    // ext1 can wake from multiple pins; good even if we add another later
    const uint64_t mask = (1ULL << (int)TP_INT_GPIO);
    esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_LOW);
  

  // 3) Go to light sleep
  esp_light_sleep_start();

 DisplayManager::instance().setScreenOn(true);
  // 4) On wake: re-enable backlight (and anything else you turned off)
  DisplayManager::instance().setBrightness(255);   // or whatever you store
}

void PowerManager::restart()
{
    ESP.restart();
}

void PowerManager::shutdown()
{
    if(!pmu_) return;

    // AXP2101: turn off SYS power rails
    pmu_->shutdown();

    // Should never return, but be defensive
    while(true) {
        delay(100);
    }
}

void PowerManager::adcOn() {
  pmu_->enableTemperatureMeasure();
  // Enable internal ADC detection
  pmu_->enableBattDetection();
  pmu_->enableVbusVoltageMeasure();
  pmu_->enableBattVoltageMeasure();
  pmu_->enableSystemVoltageMeasure();
}

void PowerManager::adcOff() {
  pmu_->disableTemperatureMeasure();
  // Disable internal ADC detection
  pmu_->disableBattDetection();
  pmu_->disableVbusVoltageMeasure();
  pmu_->disableBattVoltageMeasure();
  pmu_->disableSystemVoltageMeasure();
}