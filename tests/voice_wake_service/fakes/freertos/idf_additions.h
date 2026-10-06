#pragma once
#include "freertos/FreeRTOS.h"
inline int xTaskCreateWithCaps(TaskFunction_t fn, const char* name, uint32_t stack, void* arg,
                               uint32_t priority, TaskHandle_t* task, unsigned) {
    return xTaskCreate(fn, name, stack, arg, priority, task);
}
inline void vTaskDeleteWithCaps(TaskHandle_t task) { vTaskDelete(task); }
