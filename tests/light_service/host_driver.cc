#include "host_driver.h"
#include "board_sdk.h"
#include <algorithm>
#include <cstring>
#include <mutex>

namespace {
int strip;
std::string primary_id = "board_rgb";
dev_led_strip_logical_light_t logical_lights[] = {
    {"board_rgb", "Board RGB", 0, 3}, {"accent", "Accent", 3, 1}};
dev_led_strip_config_t config{{4}, 2, logical_lights};
dev_led_strip_handles_t handles{&strip};
std::array<fake_light::Pixel, 4> pixels;
std::mutex pixels_mutex;
}

extern "C" const esp_board_device_desc_t g_esp_board_devices[] = {
    {"rgb_light", ESP_BOARD_DEVICE_TYPE_LED_STRIP, &config, nullptr}};

namespace fake_light {
std::atomic<unsigned> pixel_calls{0}, refresh_calls{0}, clear_calls{0}, fail_pixel_call{0};
std::atomic<bool> fail_refresh{false}, fail_clear{false}, unavailable{false};
std::function<void()> before_refresh;
void Reset() {
    SetPrimaryId("board_rgb");
    pixel_calls = 0;
    refresh_calls = 0;
    clear_calls = 0;
    fail_pixel_call = 0;
    fail_refresh = false;
    fail_clear = false;
    unavailable = false;
    before_refresh = {};
    std::lock_guard<std::mutex> lock(pixels_mutex);
    pixels = {};
}
void SetPrimaryId(const std::string& id) {
    primary_id = id;
    logical_lights[0].id = primary_id.c_str();
}
std::array<Pixel, 4> Pixels() {
    std::lock_guard<std::mutex> lock(pixels_mutex);
    return pixels;
}
}

esp_err_t fake_light::DeviceConfig(const char* name, void** value) {
    if (std::strcmp(name, "rgb_light") != 0) return ESP_FAIL;
    *value = &config;
    return ESP_OK;
}
esp_err_t fake_light::DeviceHandle(const char* name, void** value) {
    if (std::strcmp(name, "rgb_light") != 0 || fake_light::unavailable) {
        *value = nullptr;
        return ESP_FAIL;
    }
    *value = &handles;
    return ESP_OK;
}
esp_err_t led_strip_clear(led_strip_handle_t) {
    ++fake_light::clear_calls;
    if (fake_light::fail_clear) return ESP_FAIL;
    std::lock_guard<std::mutex> lock(pixels_mutex);
    pixels = {};
    return ESP_OK;
}
esp_err_t led_strip_set_pixel(led_strip_handle_t, uint32_t index,
                             uint32_t red, uint32_t green, uint32_t blue) {
    const unsigned call = ++fake_light::pixel_calls;
    if (call == fake_light::fail_pixel_call || index >= pixels.size()) return ESP_FAIL;
    std::lock_guard<std::mutex> lock(pixels_mutex);
    pixels[index] = {red, green, blue};
    return ESP_OK;
}
esp_err_t led_strip_refresh(led_strip_handle_t) {
    ++fake_light::refresh_calls;
    if (fake_light::before_refresh) fake_light::before_refresh();
    return fake_light::fail_refresh ? ESP_FAIL : ESP_OK;
}
