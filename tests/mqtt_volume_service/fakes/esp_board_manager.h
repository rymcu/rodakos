#pragma once
#include <dev_audio_codec.h>
#include <esp_err.h>
inline esp_err_t esp_board_manager_init_device_by_name(const char*) {
    ++fake_codec::initializations;
    return ESP_OK;
}
inline esp_err_t esp_board_manager_deinit_device_by_name(const char*) {
    ++fake_codec::deinitializations;
    return ESP_OK;
}
inline esp_err_t esp_board_manager_get_device_handle(const char*, void** handle) {
    static dev_audio_codec_handles_t device{&fake_codec::handle};
    *handle = &device;
    return ESP_OK;
}
