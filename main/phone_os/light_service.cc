#include "phone_os/light_service.h"

#include "dev_led_strip.h"
#include "esp_board_device.h"
#include "esp_board_manager.h"
#include "esp_board_manager_defs.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <utility>

#include <esp_log.h>

namespace rodakos {
namespace {
constexpr const char* TAG = "LightService";

extern "C" const esp_board_device_desc_t g_esp_board_devices[];

const dev_led_strip_config_t* ConfigForDevice(const char* device_name) {
    if (device_name == nullptr) {
        return nullptr;
    }
    void* config = nullptr;
    const esp_err_t err = esp_board_manager_get_device_config(device_name, &config);
    if (err != ESP_OK || config == nullptr) {
        return nullptr;
    }
    return static_cast<const dev_led_strip_config_t*>(config);
}

std::string IndexedId(const char* device_name, uint32_t index, uint32_t count) {
    if (count == 1) {
        return device_name != nullptr ? device_name : "light";
    }
    return std::string(device_name != nullptr ? device_name : "light") + ":" + std::to_string(index);
}

}  // namespace

bool LightService::Init() {
    if (initialized_) {
        return IsAvailable();
    }
    initialized_ = true;
    lights_.clear();
    bindings_.clear();

    const esp_board_device_desc_t* desc = g_esp_board_devices;
    while (desc != nullptr && desc->name != nullptr) {
        if (desc->type != nullptr &&
            std::strcmp(desc->type, ESP_BOARD_DEVICE_TYPE_LED_STRIP) == 0) {
            const auto* led_config = ConfigForDevice(desc->name);
            if (led_config == nullptr) {
                ESP_LOGW(TAG, "Skipping light device '%s': missing LED strip config", desc->name);
                desc = desc->next;
                continue;
            }

            const uint32_t led_count = led_config->strip_config.max_leds;
            if (led_count == 0) {
                ESP_LOGW(TAG, "Skipping light device '%s': invalid LED count", desc->name);
                desc = desc->next;
                continue;
            }

            void* device_handle = nullptr;
            esp_err_t err = esp_board_manager_get_device_handle(desc->name, &device_handle);
            auto* handles = static_cast<dev_led_strip_handles_t*>(device_handle);
            led_strip_handle_t strip = (handles != nullptr) ? handles->strip_handle : nullptr;
            bool available = err == ESP_OK && strip != nullptr;
            if (!available) {
                ESP_LOGW(TAG, "Light device '%s' is unavailable: %s",
                         desc->name, esp_err_to_name(err));
            } else {
                err = led_strip_clear(strip);
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to clear light device '%s': %s",
                             desc->name, esp_err_to_name(err));
                    available = false;
                }
            }

            const auto add_light = [&](std::string id, std::string title,
                                       uint32_t logical_index, uint32_t first_led,
                                       uint32_t range_led_count) {
                LightState state;
                state.id = std::move(id);
                state.title = std::move(title);
                state.device_name = desc->name;
                state.logical_index = logical_index;
                state.first_led = first_led;
                state.led_count = range_led_count;
                state.available = available;
                state.enabled = false;
                state.last_error = available ? ESP_OK : err;

                bindings_.push_back(LightBinding{
                    .strip = strip,
                    .first_led = first_led,
                    .led_count = range_led_count,
                });
                lights_.push_back(std::move(state));
            };

            if (led_config->logical_light_count > 0) {
                for (uint32_t i = 0; i < led_config->logical_light_count; ++i) {
                    const auto& logical = led_config->logical_lights[i];
                    if (logical.led_count == 0 || logical.first_led >= led_count ||
                        logical.led_count > led_count - logical.first_led) {
                        ESP_LOGW(TAG,
                                 "Skipping logical light '%s' on '%s': invalid LED range %u+%u of %u",
                                 logical.id != nullptr ? logical.id : "(unnamed)",
                                 desc->name,
                                 static_cast<unsigned>(logical.first_led),
                                 static_cast<unsigned>(logical.led_count),
                                 static_cast<unsigned>(led_count));
                        continue;
                    }

                    const std::string id =
                        (logical.id != nullptr && logical.id[0] != '\0')
                            ? logical.id
                            : IndexedId(desc->name, i, led_config->logical_light_count);
                    const std::string title =
                        (logical.title != nullptr && logical.title[0] != '\0')
                            ? logical.title
                            : MakeTitle(id.c_str());
                    add_light(id, title, i, logical.first_led, logical.led_count);
                }
            } else {
                for (uint32_t led = 0; led < led_count; ++led) {
                    const std::string id = IndexedId(desc->name, led, led_count);
                    const std::string title =
                        led_count == 1 ? MakeTitle(desc->name)
                                       : MakeTitle(desc->name) + " " + std::to_string(led + 1);
                    add_light(id, title, led, led, 1);
                }
            }
        }
        desc = desc->next;
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

uint8_t LightService::Scale(const LightState& state, uint8_t channel) const {
    return static_cast<uint8_t>(
        (static_cast<uint16_t>(channel) * state.brightness_percent + 50) / 100);
}

bool LightService::ApplyLocked(size_t index) {
    if (!initialized_) {
        Init();
    }
    if (index >= lights_.size() || index >= bindings_.size()) {
        return false;
    }

    LightState& state = lights_[index];
    const LightBinding& binding = bindings_[index];
    led_strip_handle_t strip = binding.strip;
    if (strip == nullptr) {
        RememberError(index, ESP_ERR_INVALID_STATE);
        return false;
    }

    esp_err_t err = ESP_OK;
    const bool off = !state.enabled || state.brightness_percent == 0;
    for (uint32_t offset = 0; offset < binding.led_count; ++offset) {
        const uint32_t led = binding.first_led + offset;
        if (off) {
            err = led_strip_set_pixel(strip, led, 0, 0, 0);
        } else {
            err = led_strip_set_pixel(strip, led, Scale(state, state.color.red),
                                      Scale(state, state.color.green),
                                      Scale(state, state.color.blue));
        }
        if (err != ESP_OK) {
            break;
        }
    }
    if (err == ESP_OK) {
        err = led_strip_refresh(strip);
    }

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

std::string LightService::MakeTitle(const char* name) {
    if (name == nullptr || name[0] == '\0') {
        return "Light";
    }

    std::string title(name);
    for (char& ch : title) {
        if (ch == '_' || ch == '-') {
            ch = ' ';
        }
    }
    if (!title.empty()) {
        title[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(title[0])));
    }
    return title;
}

}  // namespace rodakos
