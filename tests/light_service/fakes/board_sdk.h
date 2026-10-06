#pragma once
#include <cstdint>
#include <esp_err.h>

using led_strip_handle_t = void*;
struct dev_led_strip_logical_light_t {
    const char* id;
    const char* title;
    uint32_t first_led;
    uint32_t led_count;
};
struct dev_led_strip_config_t {
    struct { uint32_t max_leds; } strip_config;
    uint32_t logical_light_count;
    const dev_led_strip_logical_light_t* logical_lights;
};
struct dev_led_strip_handles_t { led_strip_handle_t strip_handle; };
struct esp_board_device_desc_t {
    const char* name;
    const char* type;
    const void* cfg;
    const esp_board_device_desc_t* next;
};
constexpr const char* ESP_BOARD_DEVICE_TYPE_BUTTON = "button";
constexpr const char* ESP_BOARD_DEVICE_TYPE_LED_STRIP = "led_strip";
namespace fake_light {
esp_err_t DeviceConfig(const char*, void**);
esp_err_t DeviceHandle(const char*, void**);
}
esp_err_t led_strip_clear(led_strip_handle_t);
esp_err_t led_strip_set_pixel(led_strip_handle_t, uint32_t, uint32_t, uint32_t, uint32_t);
esp_err_t led_strip_refresh(led_strip_handle_t);

using button_handle_t = void*;
enum button_event_t { BUTTON_SINGLE_CLICK, BUTTON_DOUBLE_CLICK, BUTTON_LONG_PRESS_START };
struct dev_button_handles_t { uint8_t num_buttons; button_handle_t* button_handles; };
struct dev_button_config_t {
    const char* sub_type;
    struct { struct { struct { const char** button_labels; } multi; } adc; } sub_cfg;
};
inline button_event_t iot_button_get_event(button_handle_t) { return BUTTON_SINGLE_CLICK; }
inline esp_err_t iot_button_register_cb(button_handle_t, button_event_t, void*,
                                       void (*)(void*, void*), void*) { return ESP_OK; }
