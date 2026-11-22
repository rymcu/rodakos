#pragma once
#include "freertos/FreeRTOS.h"
using TaskFunction_t = void (*)(void*);
// Runs the worker inline so the storage probe has finished before the first Refresh().
inline BaseType_t xTaskCreate(TaskFunction_t task, const char*, uint32_t, void* arg, unsigned,
                              TaskHandle_t*) {
    task(arg);
    return pdPASS;
}
inline void vTaskDelete(TaskHandle_t) {}
