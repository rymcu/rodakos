// BacklightAdapter - Hardware abstraction adapter for backlight control
//
// This adapter wraps esp-brookesia HAL's LEDC device API to maintain
// compatibility with RodakOS's PhoneServices interface. It provides:
// - Brightness control (0-100%)
// - NVS persistence for user preferences
// - Seamless integration with Settings app
//
// Based on: Espressif esp-brookesia HAL (https://github.com/espressif/esp-brookesia)
// Reference: components/esp_board_manager/devices/dev_ledc_ctrl
//
#pragma once

#include <cstdint>

// BacklightAdapter wraps esp-brookesia LEDC HAL while preserving the original Backlight interface
// for compatibility with PhoneServices and Settings app.
class BacklightAdapter {
public:
    BacklightAdapter();
    ~BacklightAdapter();

    // Initialize the adapter by fetching the lcd_brightness device handle from board manager.
    // Must be called after esp_board_manager_init().
    bool Initialize();

    void SetBrightness(uint8_t brightness, bool permanent = false);
    void RestoreBrightness();
    uint8_t GetBrightness() const { return brightness_; }

private:
    void* ledc_handle_ = nullptr;  // dev_ledc_ctrl_handles_t*
    uint8_t brightness_ = 75;
    bool initialized_ = false;
};
