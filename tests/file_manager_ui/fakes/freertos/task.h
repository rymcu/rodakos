#pragma once
#include "freertos/FreeRTOS.h"
#include <functional>
using TaskFunction_t = void (*)(void*);

namespace file_manager_test {
// Tests can hold the capacity probe to check the list draws before free space is known.
inline bool hold_tasks = false;
inline std::function<void()> held_task;
inline bool reject_tasks = false;
}

// Runs the worker inline unless a test holds it, so capacity is known before the first poll.
inline BaseType_t xTaskCreate(TaskFunction_t task, const char*, uint32_t, void* arg, unsigned,
                              TaskHandle_t*) {
    if (file_manager_test::reject_tasks) return pdFAIL;
    if (file_manager_test::hold_tasks) {
        file_manager_test::held_task = [task, arg] { task(arg); };
        return pdPASS;
    }
    task(arg);
    return pdPASS;
}
inline void vTaskDelete(TaskHandle_t) {}
