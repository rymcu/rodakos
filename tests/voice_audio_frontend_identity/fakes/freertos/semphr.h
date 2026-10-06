#pragma once
#include "freertos/FreeRTOS.h"
#include <mutex>
using SemaphoreHandle_t = std::recursive_mutex*;
inline SemaphoreHandle_t xSemaphoreCreateMutex() { return new std::recursive_mutex; }
inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex() { return new std::recursive_mutex; }
inline BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t) { semaphore->lock(); return pdTRUE; }
inline BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore) { semaphore->unlock(); return pdTRUE; }
inline BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t semaphore, TickType_t) { semaphore->lock(); return pdTRUE; }
inline BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t semaphore) { semaphore->unlock(); return pdTRUE; }
inline void vSemaphoreDelete(SemaphoreHandle_t semaphore) { delete semaphore; }
