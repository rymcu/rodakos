#pragma once
#include "FreeRTOS.h"
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>
using TaskFunction_t = void (*)(void*);
namespace camera_test {
inline thread_local bool capture_worker = false;
inline std::atomic<bool> fail_task_creation{false};
inline std::mutex task_mutex;
inline std::vector<std::thread> threads;
inline void JoinTasks() {
    std::vector<std::thread> pending;
    {
        std::lock_guard<std::mutex> lock(task_mutex);
        pending.swap(threads);
    }
    for (auto& thread : pending) {
        if (thread.joinable()) thread.join();
    }
}
}
inline BaseType_t xTaskCreate(TaskFunction_t entry, const char*, uint32_t, void* data,
                             uint32_t, TaskHandle_t* handle) {
    if (camera_test::fail_task_creation) return pdFAIL;
    if (handle != nullptr) *handle = reinterpret_cast<void*>(1);
    std::lock_guard<std::mutex> lock(camera_test::task_mutex);
    camera_test::threads.emplace_back([=] {
        camera_test::capture_worker = true;
        entry(data);
    });
    return pdPASS;
}
inline void vTaskDelete(TaskHandle_t) {}
inline void vTaskDelay(TickType_t ticks) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ticks * 10));
}
