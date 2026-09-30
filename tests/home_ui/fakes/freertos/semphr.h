#pragma once

#include <mutex>

#include "FreeRTOS.h"

struct FakeSemaphore {
    std::mutex mutex;
    bool binary = false;
    bool available = false;
};

using SemaphoreHandle_t = FakeSemaphore*;

inline SemaphoreHandle_t xSemaphoreCreateMutex() {
    return new FakeSemaphore{.binary = false, .available = true};
}

inline SemaphoreHandle_t xSemaphoreCreateBinary() {
    return new FakeSemaphore{.binary = true, .available = false};
}

inline BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t) {
    if (semaphore == nullptr) return pdFALSE;
    if (!semaphore->binary) {
        semaphore->mutex.lock();
        return pdTRUE;
    }
    std::lock_guard<std::mutex> lock(semaphore->mutex);
    if (!semaphore->available) return pdFALSE;
    semaphore->available = false;
    return pdTRUE;
}

inline BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore) {
    if (semaphore == nullptr) return pdFALSE;
    if (!semaphore->binary) {
        semaphore->mutex.unlock();
        return pdTRUE;
    }
    std::lock_guard<std::mutex> lock(semaphore->mutex);
    semaphore->available = true;
    return pdTRUE;
}

inline void vSemaphoreDelete(SemaphoreHandle_t semaphore) {
    delete semaphore;
}
