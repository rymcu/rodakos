#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <esp_err.h>

namespace rodakos {

enum class BoardButtonEvent : uint8_t {
    kSingleClick,
    kDoubleClick,
    kLongPressStart,
};

struct BoardButtonDevice {
    std::string id;
    std::string device_name;
    uint8_t physical_index = 0;
    void* native_handle = nullptr;
};

struct BoardLightDevice {
    std::string id;
    std::string title;
    std::string device_name;
    uint32_t logical_index = 0;
    uint32_t first_led = 0;
    uint32_t led_count = 1;
    bool available = false;
    esp_err_t last_error = ESP_OK;
    void* native_strip = nullptr;
};

using BoardButtonEventCallback = void (*)(const char* button_id, BoardButtonEvent event, void* user_data);

std::vector<BoardButtonDevice> DiscoverBoardButtons();
bool RegisterBoardButtonCallbacks(const std::vector<BoardButtonDevice>& buttons,
                                  BoardButtonEventCallback callback,
                                  void* user_data);
std::vector<BoardLightDevice> DiscoverBoardLights();
esp_err_t ApplyBoardLight(const BoardLightDevice& light,
                          bool enabled,
                          uint8_t brightness_percent,
                          uint8_t red,
                          uint8_t green,
                          uint8_t blue);

}  // namespace rodakos
