#pragma once
#include "FreeRTOS.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>

using SemaphoreHandle_t = std::timed_mutex*;
inline SemaphoreHandle_t xSemaphoreCreateMutex() { return new std::timed_mutex; }
inline BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t) {
    if (!semaphore->try_lock_for(std::chrono::seconds(2))) {
        std::fputs("production camera semaphore was not released\n", stderr);
        std::abort();
    }
    return pdTRUE;
}
inline BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore) {
    semaphore->unlock();
    return pdTRUE;
}
inline void vSemaphoreDelete(SemaphoreHandle_t semaphore) { delete semaphore; }
