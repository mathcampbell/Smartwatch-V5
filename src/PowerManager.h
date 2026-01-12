#pragma once

#include <stdint.h>
#include <Wire.h>
#include <functional>

// Forward declare your XPowersLib PMU class type.
// In most sketches this is `XPowersAXP2101`.
class XPowersAXP2101;

class PowerManager
{
public:
    struct PowerState {
        // Power source / charging
        bool batteryConnected = false;
        bool externalPowerPresent = false;   // VBUS in
        bool vbusGood = false;
        bool charging = false;
        bool discharging = false;
        bool standby = false;

        // Charger status enum from XPowersLib (raw)
        uint8_t chargerStatus = 0;

        // Measurements (mV / °C / %)
        int16_t temperatureC = 0;
        uint16_t battVoltageMv = 0;
        uint16_t vbusVoltageMv = 0;
        uint16_t systemVoltageMv = 0;
        uint8_t batteryPercent = 0;

        // Optional: last update time
        uint32_t lastUpdateMs = 0;

        // Optional: power key events
        bool pkeyShortPressed = false;
        bool pkeyLongPressed = false; // if enabled/available
    };

    static PowerManager& instance();

    // Initialize the PMU. Returns false if PMU not found/responding.
    bool begin(TwoWire& wire, uint8_t axpAddress, int sda, int scl);

    // Call periodically from loop. Non-blocking.
    void tick();

    // Get a copy of the latest state (thread-safe enough for Arduino loop)
    PowerState state() const;

    // Convenience helpers
    bool isExternalPowerPresent() const;
    bool isCharging() const;
    uint8_t batteryPercent() const;

    // Configure how often we poll the PMU for updated values.
    void setPollIntervalMs(uint32_t ms);

    // If you have the PMU IRQ line wired to a pin, set it here.
    // When set, tick() can read/clear PMU IRQ flags and set pkeyShortPressed.
    void setPmuIrqGpio(int irqGpio, bool activeLow = true);

    // Consume (clear) key press flags (so UI can act once)
    bool consumePkeyShortPressed();
    bool consumePkeyLongPressed();
    using IrqReadFn = std::function<int(void)>;

    void setIrqReadFn(IrqReadFn fn, bool activeLow = true);
    void enterLightSleep();
    void restart();    // soft reboot
    void shutdown();   // PMU power-off

private:
    PowerManager() = default;

    void updateReadings_();
    void updateIrq_();
    void adcOn();
    void adcOff();

    mutable PowerState st_;

    // XPowers object is owned here
    XPowersAXP2101* pmu_ = nullptr;
    TwoWire* wire_ = nullptr;

    uint32_t pollIntervalMs_ = 500; // default 0.5s
    uint32_t lastPollMs_ = 0;

    int pmuIrqGpio_ = -1;
    bool pmuIrqActiveLow_ = true;
    IrqReadFn irqReadFn_;
    bool irqActiveLow_ = true;


    uint32_t keyDownMs_ = 0;
    bool     keyDown_ = false;

};
