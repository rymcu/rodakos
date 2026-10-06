#pragma once
#include "freertos/FreeRTOS.h"
using TaskFunction_t = void (*)(void*);
namespace cloud_ui_test {
inline TaskFunction_t task = nullptr;
inline void* task_arg = nullptr;
inline bool task_create_ok = true;
inline void RunTask() {
    auto callback = task; auto* arg = task_arg;
    task = nullptr; task_arg = nullptr;
    if (callback != nullptr) callback(arg);
}
}
inline BaseType_t xTaskCreate(TaskFunction_t callback, const char*, uint32_t, void* arg,
                              uint32_t, TaskHandle_t*) {
    if (!cloud_ui_test::task_create_ok) return pdFAIL;
    cloud_ui_test::task = callback; cloud_ui_test::task_arg = arg; return pdPASS;
}
inline void vTaskDelete(TaskHandle_t) {}
