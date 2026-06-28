// BacklightAdapter implementation
//
// Adapts esp-brookesia HAL LEDC device API to RodakOS's Backlight interface.
// Uses NVS for persistence and wraps periph_ledc_* functions.
//
// Based on: Espressif esp-brookesia HAL (https://github.com/espressif/esp-brookesia)
// API Reference: periph_ledc_handle_t / ledc_set_duty / ledc_update_duty
//
#include "backlight_adapter.h"
#include "settings.h"

#include <esp_board_manager.h>
#include <periph_ledc.h>
#include <driver/ledc.h>
#include <esp_log.h>

namespace {
constexpr const char* TAG = "BacklightAdapter";
}

BacklightAdapter::BacklightAdapter() = default;

BacklightAdapter::~BacklightAdapter() = default;

bool BacklightAdapter::Initialize() {
    if (initialized_) {
        return true;
    }

    esp_err_t ret = esp_board_manager_get_device_handle("lcd_brightness", &ledc_handle_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get lcd_brightness handle: %s", esp_err_to_name(ret));
        return false;
    }

    initialized_ = true;
    ESP_LOGI(TAG, "Backlight adapter initialized");
    return true;
}

void BacklightAdapter::SetBrightness(uint8_t brightness, bool permanent) {
    if (brightness > 100) {
        brightness = 100;
    }
    brightness_ = brightness;

    ESP_LOGI(TAG, "Setting brightness to %d%% (permanent=%d)", brightness_, permanent);

    if (permanent) {
        Settings settings("display", true);
        settings.SetInt("brightness", brightness_);
    }

    if (ledc_handle_ != nullptr) {
        auto handle = static_cast<periph_ledc_handle_t*>(ledc_handle_);

        // Calculate duty based on percentage (assume 13-bit resolution = 8191 max)
        uint32_t max_duty = (1 << 13) - 1;  // 8191 for 13-bit
        uint32_t duty = (brightness_ * max_duty) / 100;

        ESP_LOGI(TAG, "LEDC duty: %lu / %lu", duty, max_duty);

        esp_err_t ret = ledc_set_duty(handle->speed_mode, handle->channel, duty);
        if (ret == ESP_OK) {
            ret = ledc_update_duty(handle->speed_mode, handle->channel);
        }

        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set brightness: %s", esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "Brightness set successfully");
        }
    } else {
        ESP_LOGW(TAG, "LEDC handle is null, cannot set brightness");
    }
}

void BacklightAdapter::RestoreBrightness() {
    Settings settings("display", false);
    int brightness = settings.GetInt("brightness", 75);
    if (brightness < 5) {
        brightness = 75;
    }
    ESP_LOGI(TAG, "Restoring brightness to %d%%", brightness);
    SetBrightness(static_cast<uint8_t>(brightness), false);
}
