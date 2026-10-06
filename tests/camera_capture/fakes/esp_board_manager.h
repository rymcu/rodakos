#pragma once
#include <esp_err.h>
esp_err_t esp_board_manager_init_device_by_name(const char*);
esp_err_t esp_board_manager_deinit_device_by_name(const char*);
esp_err_t esp_board_manager_get_device_handle(const char*, void**);
