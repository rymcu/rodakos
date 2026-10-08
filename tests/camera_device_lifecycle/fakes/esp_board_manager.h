#pragma once
#include "esp_err.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t esp_board_manager_init_device_by_name(const char*);
esp_err_t esp_board_manager_deinit_device_by_name(const char*);
esp_err_t esp_board_manager_get_device_handle(const char*, void**);
bool esp_board_manager_check_name(const char*);
#ifdef __cplusplus
}
#endif
