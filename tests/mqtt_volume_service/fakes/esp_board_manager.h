#pragma once
#include <dev_audio_codec.h>
#include <esp_err.h>
#include "board_sdk.h"
#include <cstring>
inline esp_err_t esp_board_manager_get_device_config(const char* name, void** value) {
    return fake_light::DeviceConfig(name, value);
}
inline esp_err_t esp_board_manager_init_device_by_name(const char*) {
    ++fake_codec::initializations;
    return ESP_OK;
}
inline esp_err_t esp_board_manager_deinit_device_by_name(const char*) {
    ++fake_codec::deinitializations;
    return ESP_OK;
}
inline esp_err_t esp_board_manager_get_device_handle(const char* name, void** handle) {
    if (std::strcmp(name, "rgb_light") == 0) return fake_light::DeviceHandle(name, handle);
    static dev_audio_codec_handles_t device{&fake_codec::handle};
    *handle = &device;
    return ESP_OK;
}
