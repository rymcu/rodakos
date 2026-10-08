#pragma once
#include "freertos/FreeRTOS.h"
inline constexpr int ESP_OK = 0;
using esp_freertos_tick_cb_t = void (*)();
int esp_register_freertos_tick_hook_for_cpu(esp_freertos_tick_cb_t, UBaseType_t);
void esp_deregister_freertos_tick_hook_for_cpu(esp_freertos_tick_cb_t, UBaseType_t);
