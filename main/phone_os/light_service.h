#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "esp_err.h"
#include "rodakos_adapters/board_device_adapter.h"

namespace rodakos {

struct RgbColor {
    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;
};

enum class LightApplication { kDriverApplied, kUnverified };

struct LightConfiguration {
    bool enabled = false;
    uint8_t brightness_percent = 60;
    RgbColor color{32, 160, 255};
};

struct LightColorPatch {
    std::optional<uint8_t> red;
    std::optional<uint8_t> green;
    std::optional<uint8_t> blue;
    LightColorPatch() = default;
    LightColorPatch(RgbColor color) : red(color.red), green(color.green), blue(color.blue) {}
};

struct LightPatch {
    std::optional<bool> enabled;
    std::optional<int> brightness_percent;
    std::optional<LightColorPatch> color;
};

struct LightApplyResult {
    bool accepted = false;
    std::string id;
    LightConfiguration previous;
    LightConfiguration state;
    uint32_t configuration_revision = 0;
    LightApplication application = LightApplication::kUnverified;
    std::string error_code;
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
    uint32_t configuration_revision = 0;
    LightApplication application = LightApplication::kUnverified;
};

class LightService {
public:
    bool Init();
    bool IsAvailable() const;

    std::vector<LightState> ListLights() const;
    bool GetLight(size_t index, LightState& state) const;
    LightApplyResult ApplyLightPatch(size_t index, const LightPatch& patch);
    LightApplyResult ApplyLightPatch(const std::string& id, const LightPatch& patch);

    bool SetEnabled(size_t index, bool enabled);
    bool Toggle(size_t index);
    bool SetBrightness(size_t index, uint8_t brightness_percent);
    bool SetColor(size_t index, RgbColor color);
    bool SetState(size_t index, bool enabled, uint8_t brightness_percent, RgbColor color);
    bool Apply(size_t index);

private:
    LightApplyResult ApplyLightPatchLocked(size_t index, const LightPatch& patch);

    std::vector<LightState> lights_;
    std::vector<BoardLightDevice> board_lights_;
    bool initialized_ = false;
    mutable std::recursive_mutex mutex_;
};

}  // namespace rodakos
