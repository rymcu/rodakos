#pragma once
#include "FreeRTOS.h"
#include <cstdint>
using TaskHandle_t = void*;
using TaskFunction_t = void (*)(void*);
BaseType_t xTaskCreatePinnedToCoreWithCaps(TaskFunction_t, const char*, uint32_t, void*, uint32_t,
                                         TaskHandle_t*, int, uint32_t);
BaseType_t xTaskCreateWithCaps(TaskFunction_t, const char*, uint32_t, void*, uint32_t,
                             TaskHandle_t*, uint32_t);
TaskHandle_t xTaskGetCurrentTaskHandle();
void vTaskDelay(TickType_t);
inline void vTaskDeleteWithCaps(TaskHandle_t) {}
