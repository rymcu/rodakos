#pragma once

#include <freertos/FreeRTOS.h>
#include <mutex>

using SemaphoreHandle_t = std::mutex*;
inline SemaphoreHandle_t xSemaphoreCreateMutex() { return new std::mutex; }
inline BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t) {
    semaphore->lock();
    return pdTRUE;
}
inline BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore) {
    semaphore->unlock();
    return pdTRUE;
}
inline void vSemaphoreDelete(SemaphoreHandle_t semaphore) { delete semaphore; }
