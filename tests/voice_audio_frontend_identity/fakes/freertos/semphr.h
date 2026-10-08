#pragma once
#include "freertos/FreeRTOS.h"
#include <mutex>
#include "observation_control.h"
struct HostSemaphore {
    std::mutex ordinary;
    std::recursive_mutex recursive;
};
using SemaphoreHandle_t = HostSemaphore*;
inline SemaphoreHandle_t xSemaphoreCreateMutex() { return new HostSemaphore; }
inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex() { return new HostSemaphore; }
inline BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t) {
    using namespace rodakos_test::afe_observation;
    OnSemaphore(semaphore, SemaphorePoint::kBeforeTake);
    semaphore->ordinary.lock();
    OnSemaphore(semaphore, SemaphorePoint::kAfterTake);
    return pdTRUE;
}
inline BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore) {
    semaphore->ordinary.unlock();
    rodakos_test::afe_observation::OnSemaphore(semaphore,
        rodakos_test::afe_observation::SemaphorePoint::kAfterGive);
    return pdTRUE;
}
inline BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t semaphore, TickType_t) { semaphore->recursive.lock(); return pdTRUE; }
inline BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t semaphore) { semaphore->recursive.unlock(); return pdTRUE; }
inline void vSemaphoreDelete(SemaphoreHandle_t semaphore) { delete semaphore; }
