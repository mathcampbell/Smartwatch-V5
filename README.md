ESP32-S3 Smartwatch

This project runs on the Waveshare ESP32-S3 Touch AMOLED 1.75" board and provides a simple standalone smartwatch UI using the Arduino framework, PlatformIO, and LVGL. It drives the round AMOLED display, handles the integrated touch panel, manages brightness, dimming, and sleep/wake behaviour, and loads user settings from LittleFS so the watch remembers brightness and timeout preferences across reboots.

The codebase is structured around small managers (DisplayManager, PowerManager, SettingsManager, etc.) to keep things modular. The watch face updates cleanly after wake, touch input wakes the device, and the system triggers full LVGL redraws when needed to avoid ghosting or outdated UI elements. The project is intended as a practical foundation for custom watch faces, sensor integration, notifications, and additional features over time.

To build, open the project in PlatformIO, select the Waveshare ESP32-S3 board configuration, and upload the firmware. Flashing LittleFS is required once to create the initial settings file. This repository is meant as a starting point or reference implementation for anyone experimenting with LVGL on ESP32-S3 AMOLED hardware.
