#include "rodakos_adapters/board_device_adapter.h"

#include <cctype>
#include <cstring>
#include <memory>
#include <utility>

#include <dev_button.h>
#include <dev_led_strip.h>
#include <esp_board_device.h>
#include <esp_board_manager.h>
#include <esp_board_manager_defs.h>
#include <esp_log.h>
#include <iot_button.h>

extern "C" const esp_board_device_desc_t g_esp_board_devices[];

namespace rodakos {
namespace {
constexpr const char* TAG = "BoardDeviceAdapter";

struct BoardButtonCallbackContext {
    std::string button_id;
    BoardButtonEventCallback callback = nullptr;
    void* user_data = nullptr;
};

std::vector<std::unique_ptr<BoardButtonCallbackContext>>& ButtonCallbackContexts() {
    static std::vector<std::unique_ptr<BoardButtonCallbackContext>> contexts;
    return contexts;
}

const char* ButtonEventName(button_event_t event) {
    switch (event) {
        case BUTTON_SINGLE_CLICK:
            return "single";
        case BUTTON_DOUBLE_CLICK:
            return "double";
        case BUTTON_LONG_PRESS_START:
            return "long";
        default:
            return "unknown";
    }
}

bool ToBoardButtonEvent(button_event_t event, BoardButtonEvent& out) {
    switch (event) {
        case BUTTON_SINGLE_CLICK:
            out = BoardButtonEvent::kSingleClick;
            return true;
        case BUTTON_DOUBLE_CLICK:
            out = BoardButtonEvent::kDoubleClick;
            return true;
        case BUTTON_LONG_PRESS_START:
            out = BoardButtonEvent::kLongPressStart;
            return true;
        default:
            return false;
    }
}

void ButtonEventBridge(void* button_handle, void* user_data) {
    auto* context = static_cast<BoardButtonCallbackContext*>(user_data);
    if (context == nullptr || context->callback == nullptr) {
        return;
    }

    const button_event_t event = iot_button_get_event(static_cast<button_handle_t>(button_handle));
    BoardButtonEvent board_event = BoardButtonEvent::kSingleClick;
    if (!ToBoardButtonEvent(event, board_event)) {
        return;
    }
    context->callback(context->button_id.c_str(), board_event, context->user_data);
}

std::string IndexedId(const char* device_name, uint32_t index, uint32_t count) {
    if (count == 1) {
        return device_name != nullptr ? device_name : "light";
    }
    return std::string(device_name != nullptr ? device_name : "light") + ":" + std::to_string(index);
}

std::string MakeLightTitle(const char* name) {
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

const dev_led_strip_config_t* LedConfigForDevice(const char* device_name) {
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

uint8_t ScaleColor(uint8_t channel, uint8_t brightness_percent) {
    return static_cast<uint8_t>(
        (static_cast<uint16_t>(channel) * brightness_percent + 50) / 100);
}
}  // namespace

std::vector<BoardButtonDevice> DiscoverBoardButtons() {
    std::vector<BoardButtonDevice> buttons;

    const esp_board_device_desc_t* desc = g_esp_board_devices;
    while (desc != nullptr && desc->name != nullptr) {
        if (desc->type == nullptr ||
            std::strcmp(desc->type, ESP_BOARD_DEVICE_TYPE_BUTTON) != 0) {
            desc = desc->next;
            continue;
        }

        void* device_handle = nullptr;
        const esp_err_t err = esp_board_manager_get_device_handle(desc->name, &device_handle);
        auto* handles = static_cast<dev_button_handles_t*>(device_handle);
        if (err != ESP_OK || handles == nullptr || handles->num_buttons == 0) {
            ESP_LOGW(TAG, "Button device '%s' is unavailable: %s",
                     desc->name, esp_err_to_name(err));
            desc = desc->next;
            continue;
        }

        const auto* cfg = static_cast<const dev_button_config_t*>(desc->cfg);
        for (uint8_t i = 0; i < handles->num_buttons; ++i) {
            button_handle_t handle = handles->button_handles[i];
            if (handle == nullptr) {
                continue;
            }

            std::string id = desc->name;
            if (handles->num_buttons > 1) {
                const char* label = nullptr;
                if (cfg != nullptr &&
                    cfg->sub_type != nullptr &&
                    std::strcmp(cfg->sub_type, "adc_multi") == 0) {
                    label = cfg->sub_cfg.adc.multi.button_labels[i];
                }
                id = (label != nullptr && label[0] != '\0')
                         ? label
                         : std::string(desc->name) + ":" + std::to_string(i);
            }

            buttons.push_back(BoardButtonDevice{
                .id = std::move(id),
                .device_name = desc->name,
                .physical_index = i,
                .native_handle = handle,
            });
        }

        desc = desc->next;
    }

    return buttons;
}

bool RegisterBoardButtonCallbacks(const std::vector<BoardButtonDevice>& buttons,
                                  BoardButtonEventCallback callback,
                                  void* user_data) {
    if (callback == nullptr) {
        return false;
    }

    bool registered_any = false;
    for (const auto& button : buttons) {
        auto handle = static_cast<button_handle_t>(button.native_handle);
        if (handle == nullptr) {
            continue;
        }

        auto context = std::make_unique<BoardButtonCallbackContext>();
        context->button_id = button.id;
        context->callback = callback;
        context->user_data = user_data;
        auto* context_ptr = context.get();
        ButtonCallbackContexts().push_back(std::move(context));

        for (button_event_t event : {BUTTON_SINGLE_CLICK, BUTTON_DOUBLE_CLICK, BUTTON_LONG_PRESS_START}) {
            const esp_err_t err = iot_button_register_cb(
                handle, event, nullptr, ButtonEventBridge, context_ptr);
            if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
                registered_any = true;
                ESP_LOGI(TAG, "Listening for '%s' %s", button.id.c_str(), ButtonEventName(event));
            } else {
                ESP_LOGW(TAG, "Failed to register %s for '%s': %s",
                         ButtonEventName(event), button.id.c_str(), esp_err_to_name(err));
            }
        }
    }

    return registered_any;
}

std::vector<BoardLightDevice> DiscoverBoardLights() {
    std::vector<BoardLightDevice> lights;

    const esp_board_device_desc_t* desc = g_esp_board_devices;
    while (desc != nullptr && desc->name != nullptr) {
        if (desc->type == nullptr ||
            std::strcmp(desc->type, ESP_BOARD_DEVICE_TYPE_LED_STRIP) != 0) {
            desc = desc->next;
            continue;
        }

        const auto* led_config = LedConfigForDevice(desc->name);
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
            lights.push_back(BoardLightDevice{
                .id = std::move(id),
                .title = std::move(title),
                .device_name = desc->name,
                .logical_index = logical_index,
                .first_led = first_led,
                .led_count = range_led_count,
                .available = available,
                .last_error = available ? ESP_OK : err,
                .native_strip = strip,
            });
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
                        : MakeLightTitle(id.c_str());
                add_light(id, title, i, logical.first_led, logical.led_count);
            }
        } else {
            for (uint32_t led = 0; led < led_count; ++led) {
                const std::string id = IndexedId(desc->name, led, led_count);
                const std::string title =
                    led_count == 1 ? MakeLightTitle(desc->name)
                                   : MakeLightTitle(desc->name) + " " + std::to_string(led + 1);
                add_light(id, title, led, led, 1);
            }
        }

        desc = desc->next;
    }

    return lights;
}

esp_err_t ApplyBoardLight(const BoardLightDevice& light,
                          bool enabled,
                          uint8_t brightness_percent,
                          uint8_t red,
                          uint8_t green,
                          uint8_t blue) {
    auto strip = static_cast<led_strip_handle_t>(light.native_strip);
    if (strip == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ESP_OK;
    const bool off = !enabled || brightness_percent == 0;
    for (uint32_t offset = 0; offset < light.led_count; ++offset) {
        const uint32_t led = light.first_led + offset;
        if (off) {
            err = led_strip_set_pixel(strip, led, 0, 0, 0);
        } else {
            err = led_strip_set_pixel(strip, led, ScaleColor(red, brightness_percent),
                                      ScaleColor(green, brightness_percent),
                                      ScaleColor(blue, brightness_percent));
        }
        if (err != ESP_OK) {
            break;
        }
    }
    if (err == ESP_OK) {
        err = led_strip_refresh(strip);
    }
    return err;
}

}  // namespace rodakos
