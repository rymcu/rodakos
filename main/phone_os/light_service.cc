#include "phone_os/light_service.h"

#include "rodakos_adapters/board_device_adapter.h"

#include <algorithm>
#include <limits>
#include <utility>

#include <esp_log.h>

namespace rodakos {
namespace {
constexpr const char* TAG = "LightService";

LightConfiguration Configuration(const LightState& state) {
    return {state.enabled, state.brightness_percent, state.color};
}
}  // namespace

bool LightService::Init() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (initialized_) return IsAvailable();
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
        state.last_error = device.last_error;
        state.application = device.available ? LightApplication::kDriverApplied
                                             : LightApplication::kUnverified;
        board_lights_.push_back(device);
        lights_.push_back(std::move(state));
    }

    ESP_LOGI(TAG, "Discovered %u board light(s)", static_cast<unsigned>(lights_.size()));
    return IsAvailable();
}

bool LightService::IsAvailable() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return !lights_.empty();
}

std::vector<LightState> LightService::ListLights() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return lights_;
}

bool LightService::GetLight(size_t index, LightState& state) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (index >= lights_.size()) return false;
    state = lights_[index];
    return true;
}

bool LightService::SetEnabled(size_t index, bool enabled) {
    LightPatch patch;
    patch.enabled = enabled;
    return ApplyLightPatch(index, patch).accepted;
}

bool LightService::Toggle(size_t index) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!initialized_) Init();
    if (index >= lights_.size()) return false;
    LightPatch patch;
    patch.enabled = !lights_[index].enabled;
    return ApplyLightPatchLocked(index, patch).accepted;
}

bool LightService::SetBrightness(size_t index, uint8_t brightness_percent) {
    LightPatch patch;
    patch.brightness_percent = std::min<uint8_t>(brightness_percent, 100);
    return ApplyLightPatch(index, patch).accepted;
}

bool LightService::SetColor(size_t index, RgbColor color) {
    LightPatch patch;
    patch.color = color;
    patch.enabled = true;
    return ApplyLightPatch(index, patch).accepted;
}

bool LightService::SetState(size_t index, bool enabled, uint8_t brightness_percent, RgbColor color) {
    LightPatch patch;
    patch.enabled = enabled;
    patch.brightness_percent = std::min<uint8_t>(brightness_percent, 100);
    patch.color = color;
    return ApplyLightPatch(index, patch).accepted;
}

bool LightService::Apply(size_t index) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!initialized_) Init();
    if (index >= lights_.size()) return false;
    LightPatch patch;
    patch.enabled = lights_[index].enabled;
    return ApplyLightPatchLocked(index, patch).accepted;
}

LightApplyResult LightService::ApplyLightPatch(size_t index, const LightPatch& patch) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return ApplyLightPatchLocked(index, patch);
}

LightApplyResult LightService::ApplyLightPatch(const std::string& id, const LightPatch& patch) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!initialized_) Init();
    for (size_t index = 0; index < lights_.size(); ++index) {
        if (lights_[index].id == id) return ApplyLightPatchLocked(index, patch);
    }
    LightApplyResult result;
    result.id = id;
    result.error_code = "unknown-light";
    return result;
}

LightApplyResult LightService::ApplyLightPatchLocked(size_t index, const LightPatch& patch) {
    if (!initialized_) Init();
    LightApplyResult result;
    if (index >= lights_.size() || index >= board_lights_.size()) {
        result.error_code = "unknown-light";
        return result;
    }

    LightState& state = lights_[index];
    result.id = state.id;
    result.previous = Configuration(state);
    result.state = result.previous;
    result.configuration_revision = state.configuration_revision;
    if ((!patch.enabled && !patch.brightness_percent && !patch.color) ||
        (patch.color && !patch.color->red && !patch.color->green && !patch.color->blue) ||
        (patch.brightness_percent &&
         (*patch.brightness_percent < 0 || *patch.brightness_percent > 100))) {
        result.error_code = "invalid-patch";
        return result;
    }
    if (state.configuration_revision == std::numeric_limits<uint32_t>::max()) {
        result.error_code = "revision-exhausted";
        return result;
    }

    LightConfiguration next = result.previous;
    if (patch.enabled) next.enabled = *patch.enabled;
    if (patch.brightness_percent)
        next.brightness_percent = static_cast<uint8_t>(*patch.brightness_percent);
    if (patch.color) {
        if (patch.color->red) next.color.red = *patch.color->red;
        if (patch.color->green) next.color.green = *patch.color->green;
        if (patch.color->blue) next.color.blue = *patch.color->blue;
    }
    const esp_err_t err = ApplyBoardLight(board_lights_[index], next.enabled,
                                          next.brightness_percent, next.color.red,
                                          next.color.green, next.color.blue);
    if (err != ESP_OK) {
        // A failed driver operation may already have changed its buffer or physical LEDs.
        // Keep the last accepted software configuration without claiming hardware rollback.
        state.available = false;
        state.last_error = err;
        state.application = LightApplication::kUnverified;
        result.error_code = board_lights_[index].native_strip == nullptr
                                ? "driver-unavailable" : "driver-write-failed";
        ESP_LOGW(TAG, "Failed to apply light '%s': %s", state.id.c_str(), esp_err_to_name(err));
        return result;
    }

    state.enabled = next.enabled;
    state.brightness_percent = next.brightness_percent;
    state.color = next.color;
    ++state.configuration_revision;
    state.available = true;
    state.last_error = ESP_OK;
    state.application = LightApplication::kDriverApplied;
    result.accepted = true;
    result.state = next;
    result.configuration_revision = state.configuration_revision;
    result.application = state.application;
    return result;
}

}  // namespace rodakos
