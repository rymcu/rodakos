#pragma once

#include "FreeRTOS.h"

using TaskFunction_t = void (*)(void*);

inline BaseType_t xTaskCreateWithCaps(TaskFunction_t, const char*, uint32_t, void*,
                                      uint32_t, TaskHandle_t*, uint32_t) {
    return pdFAIL;
}

inline TaskHandle_t xTaskGetCurrentTaskHandle() {
    return nullptr;
}

inline void vTaskDeleteWithCaps(TaskHandle_t) {}
inline void vTaskDelay(TickType_t) {}
