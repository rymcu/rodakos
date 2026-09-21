#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <mutex>

#include "esp_err.h"
#include "rodakos_adapters/board_device_adapter.h"

namespace rodakos {

struct RgbColor {
    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;
};

struct LightState {
    std::string id;
    std::string title;
    std::string device_name;
    uint32_t logical_index = 0;
    uint32_t first_led = 0;
    uint32_t led_count = 1;
    bool available = false;
    bool enabled = false;
    uint8_t brightness_percent = 60;
    RgbColor color{32, 160, 255};
    esp_err_t last_error = ESP_OK;
};

class LightService {
public:
    bool Init();
    bool IsAvailable() const;

    std::vector<LightState> ListLights() const;
    bool GetLight(size_t index, LightState& state) const;

    bool SetEnabled(size_t index, bool enabled);
    bool Toggle(size_t index);
    bool SetBrightness(size_t index, uint8_t brightness_percent);
    bool SetColor(size_t index, RgbColor color);
    bool SetState(size_t index, bool enabled, uint8_t brightness_percent, RgbColor color);
    bool Apply(size_t index);

private:
    bool ApplyLocked(size_t index);
    void RememberError(size_t index, esp_err_t err);

    std::vector<LightState> lights_;
    std::vector<BoardLightDevice> board_lights_;
    bool initialized_ = false;
    mutable std::recursive_mutex mutex_;
};

}  // namespace rodakos
