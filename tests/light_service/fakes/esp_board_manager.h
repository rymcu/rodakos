#pragma once
#include "board_sdk.h"
inline esp_err_t esp_board_manager_get_device_config(const char* name, void** value) {
    return fake_light::DeviceConfig(name, value);
}
inline esp_err_t esp_board_manager_get_device_handle(const char* name, void** value) {
    return fake_light::DeviceHandle(name, value);
}
