#pragma once
// Host fake for the Smart UI tests: same public surface the app uses from
// main/phone_os/light_service.h, backed by plain state so tests can script lights, overlays
// and driver failures and inspect every patch the app sends.
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rodakos {

struct RgbColor {
    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;
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
    std::string error_code;
};

struct LightState {
    std::string id;
    std::string title;
    bool available = true;
    bool enabled = false;
    uint8_t brightness_percent = 60;
    RgbColor color{32, 160, 255};
    int last_error = 0;
};

struct RecordedPatch {
    std::string id;
    LightPatch patch;
};

class LightService {
public:
    bool Init() { ++init_calls; return !lights.empty(); }
    std::vector<LightState> ListLights() const { return lights; }
    bool GetLight(size_t index, LightState& state) const {
        if (index >= lights.size()) return false;
        state = lights[index];
        return true;
    }
    bool HasOverlay(size_t index) const { return index < overlay.size() && overlay[index]; }

    LightApplyResult ApplyLightPatch(const std::string& id, const LightPatch& patch) {
        patches.push_back({id, patch});
        LightApplyResult result;
        result.id = id;
        for (auto& light : lights) {
            if (light.id != id) continue;
            if (fail_writes) {
                light.available = false;
                result.error_code = "driver-write-failed";
                return result;
            }
            if (patch.enabled) light.enabled = *patch.enabled;
            if (patch.brightness_percent) {
                light.brightness_percent = static_cast<uint8_t>(*patch.brightness_percent);
            }
            if (patch.color) {
                if (patch.color->red) light.color.red = *patch.color->red;
                if (patch.color->green) light.color.green = *patch.color->green;
                if (patch.color->blue) light.color.blue = *patch.color->blue;
            }
            light.available = true;
            result.accepted = true;
            return result;
        }
        result.error_code = "unknown-light";
        return result;
    }

    std::vector<LightState> lights;
    std::vector<bool> overlay;
    std::vector<RecordedPatch> patches;
    bool fail_writes = false;
    int init_calls = 0;
};

}  // namespace rodakos
