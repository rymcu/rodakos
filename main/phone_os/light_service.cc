#include "phone_os/light_service.h"

#include "rodakos_adapters/board_device_adapter.h"

#include <algorithm>
#include <utility>

#include <esp_log.h>

namespace rodakos {
namespace {
constexpr const char* TAG = "LightService";
}  // namespace

bool LightService::Init() {
    if (initialized_) {
        return IsAvailable();
    }
    initialized_ = true;
    lights_.clear();
    board_lights_.clear();

    for (const auto& device : DiscoverBoardLights()) {
        LightState state;
        state.id = device.id;
        state.title = device.title;
        state.device_name = device.device_name;
        state.logical_index = device.logical_index;
        state.first_led = device.first_led;
        state.led_count = device.led_count;
        state.available = device.available;
        state.enabled = false;
        state.last_error = device.last_error;

        board_lights_.push_back(device);
        lights_.push_back(std::move(state));
    }

    ESP_LOGI(TAG, "Discovered %u board light(s)", static_cast<unsigned>(lights_.size()));
    return IsAvailable();
}

const LightState* LightService::GetLight(size_t index) const {
    if (index >= lights_.size()) {
        return nullptr;
    }
    return &lights_[index];
}

bool LightService::SetEnabled(size_t index, bool enabled) {
    if (index >= lights_.size()) {
        return false;
    }
    lights_[index].enabled = enabled;
    return ApplyLocked(index);
}

bool LightService::Toggle(size_t index) {
    if (index >= lights_.size()) {
        return false;
    }
    return SetEnabled(index, !lights_[index].enabled);
}

bool LightService::SetBrightness(size_t index, uint8_t brightness_percent) {
    if (index >= lights_.size()) {
        return false;
    }
    lights_[index].brightness_percent = std::min<uint8_t>(brightness_percent, 100);
    return ApplyLocked(index);
}

bool LightService::SetColor(size_t index, RgbColor color) {
    if (index >= lights_.size()) {
        return false;
    }
    lights_[index].color = color;
    if (!lights_[index].enabled) {
        lights_[index].enabled = true;
    }
    return ApplyLocked(index);
}

bool LightService::Apply(size_t index) {
    return ApplyLocked(index);
}

bool LightService::ApplyLocked(size_t index) {
    if (!initialized_) {
        Init();
    }
    if (index >= lights_.size() || index >= board_lights_.size()) {
        return false;
    }

    LightState& state = lights_[index];
    const esp_err_t err = ApplyBoardLight(board_lights_[index],
                                          state.enabled,
                                          state.brightness_percent,
                                          state.color.red,
                                          state.color.green,
                                          state.color.blue);

    if (err != ESP_OK) {
        RememberError(index, err);
        ESP_LOGW(TAG, "Failed to apply light '%s': %s",
                 state.id.c_str(), esp_err_to_name(err));
        return false;
    }

    state.available = true;
    state.last_error = ESP_OK;
    return true;
}

void LightService::RememberError(size_t index, esp_err_t err) {
    if (index < lights_.size()) {
        lights_[index].available = false;
        lights_[index].last_error = err;
    }
}

}  // namespace rodakos
