#pragma once
#include "freertos/FreeRTOS.h"
#include <mutex>
struct HostSemaphore { std::mutex mutex; };
using SemaphoreHandle_t = HostSemaphore*;
inline SemaphoreHandle_t xSemaphoreCreateMutex() { return new HostSemaphore; }
inline BaseType_t xSemaphoreTake(SemaphoreHandle_t value, TickType_t) {
    value->mutex.lock(); return pdTRUE;
}
inline BaseType_t xSemaphoreGive(SemaphoreHandle_t value) {
    value->mutex.unlock(); return pdTRUE;
}
inline void vSemaphoreDelete(SemaphoreHandle_t value) { delete value; }
