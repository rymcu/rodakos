#pragma once
#include "freertos/FreeRTOS.h"
inline constexpr int ESP_OK = 0;
using FrontendObserverTickHook = void (*)();
int frontend_observer_register_hook(FrontendObserverTickHook, UBaseType_t);
void frontend_observer_deregister_hook(FrontendObserverTickHook, UBaseType_t);
#define esp_register_freertos_tick_hook_for_cpu frontend_observer_register_hook
#define esp_deregister_freertos_tick_hook_for_cpu frontend_observer_deregister_hook
