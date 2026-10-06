#pragma once

#include <freertos/FreeRTOS.h>

using TaskHandle_t = void*;
using TaskFunction_t = void (*)(void*);
TaskHandle_t xTaskGetCurrentTaskHandle();
BaseType_t xTaskCreate(TaskFunction_t entry, const char*, uint32_t, void* argument,
                       UBaseType_t, TaskHandle_t* handle);
void vTaskDelay(TickType_t ticks);
inline void vTaskDelete(TaskHandle_t) {}
inline UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t) { return 8192; }
